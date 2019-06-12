/*
 * virt_cacard.c A program making use of libcacard and vpcd
 * to present a virtual CACard through PC/SC API
 *
 * Copyright (c) 2019 Red Hat
 *
 * Author(s): Pierre-Louis Palant <ppalant@redhat.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <libcacard.h>

#include "connection.h"
#include "glib-compat.h"

#define ARGS "db=\"sql:%s\" use_hw=removable" //no need for soft options with use_hw=removable

typedef enum convmode{
    HEX2BYTES = 0,
    BYTES2HEX
} convmode;

static GMainLoop *loop;
static GThread *thread;
static guint nreaders;
static GMutex mutex;
static GCond cond;
static GIOChannel *channel_socket;
static GByteArray *socket_to_send;
static CompatGMutex socket_to_send_lock;

static guint socket_tag_read, socket_tag_send ;

/** 
 **  the reader name is automatically detected 
 **/
static const char* reader_name; 
static const char hostname[] = "127.0.0.1";

static gpointer events_thread(gpointer data)
{
    vreader_id_t reader_id;
    VEvent *event;

    while(TRUE)
    {
        event = vevent_wait_next_vevent();

        if (event == NULL)
        {
            break;
        }
        if (event->type == VEVENT_LAST) {
            vevent_delete(event);
            break;
        }
        reader_id = vreader_get_id(event->reader);
        if (reader_id == VSCARD_UNDEFINED_READER_ID) 
        {
            g_mutex_lock(&mutex);
            vreader_set_id(event->reader, nreaders++);
            g_cond_signal(&cond);
            g_mutex_unlock(&mutex);
            reader_id = vreader_get_id(event->reader);
        }
        switch (event->type) 
        {
            case VEVENT_READER_INSERT:
                printf("REMOVE card \tReader name: %s\n", vreader_get_name(event->reader));
                break;
            case VEVENT_READER_REMOVE:
                printf("REMOVE reader\n\n\n\n");
                break;
            case VEVENT_CARD_INSERT:
                printf("INSERT card\n\n\n\n");
                break;
            case VEVENT_CARD_REMOVE:
                printf("REMOVE card \tReader name: %s\n", vreader_get_name(event->reader));
                break;
            case VEVENT_LAST:
            default:
                g_warn_if_reached();
                break;
        }
        vevent_delete(event);
    }

    return NULL;
}

/**
 * Sets the global reader name
 * returns TRUE if name has been set,
 * returns FALSE otherwise
 **/
static gboolean set_reader_name(void){
    VReaderList *relist = vreader_get_reader_list();
    VReaderListEntry *rlentry;
    VReader *r;
    gboolean isSetname = FALSE;

    if(relist != NULL){
        rlentry = vreader_list_get_first(relist);
        while(rlentry != NULL){
            r = vreader_list_get_reader(rlentry);
            if(vreader_card_is_present(r) != VREADER_NO_CARD){ 
                reader_name = vreader_get_name(r);
                isSetname = (reader_name!=NULL) ? TRUE : FALSE;
                break;
            }
            rlentry = vreader_list_get_next(rlentry);
        }
        vreader_free(r);
        vreader_list_delete(relist);
    }
    return isSetname;
}

/**
 * initializes virtual card with given options
 * Only tested with hw=removable and softhsm2 token
 **/
static VCardEmulError init_cacard(void)
{
    char *dbdir = g_build_filename("db",NULL);
    char *args = g_strdup_printf(ARGS,dbdir);
    VCardEmulOptions *command_line_options = NULL;
    VCardEmulError ret = VCARD_EMUL_FAIL;
    VReader *r;

    // Start the event thread
    thread = g_thread_new("tinker/events", events_thread, NULL);

    printf(" Init card \n");
    command_line_options = vcard_emul_options(args);
    ret = vcard_emul_init(command_line_options);
    if(ret != VCARD_EMUL_OK){
        printf("Failed to init card\n");
    }
    else{
        printf("Init ok with options \"%s\"\n", args);
        if(set_reader_name()) 
            r = vreader_get_reader_by_name(reader_name);

        if(r == NULL){
            printf("No readers with a card present. You may have forgotten to create soft token.\nIf you did, check if you have set SOFTHSM2_CONF env variable ?\n");
            ret = VCARD_EMUL_FAIL;
        }else{
            vreader_free(r); /* get by name ref */
            g_mutex_lock(&mutex);
            while (nreaders <= 1)
                g_cond_wait(&cond, &mutex);
        }
    }
    g_free(args);
    g_free(dbdir);
    return ret;
}


void print_apdu(uint8_t *apdu, int length){
    printf("APDU:\t");
    for(int i = 0; i < length; i++){
        printf("0x%x ", apdu[i]);
    }
    printf("\n");
}


/**
 * Send an already formatted message across the GIOChannel to pcscd
 **/
static gboolean do_socket_send(GIOChannel *source, GIOCondition condition, gpointer data)
{
    gsize bw;
    GError *error = NULL;

    g_return_val_if_fail(socket_to_send->len != 0, FALSE);
    g_return_val_if_fail(condition & G_IO_OUT, FALSE);
    if (condition & G_IO_HUP)
        g_error ("Write end of pipe died!\n");

    g_io_channel_write_chars(channel_socket, (gchar *)socket_to_send->data, socket_to_send->len, &bw, &error);
    if (error != NULL) {
        g_error("Error while sending socket %s", error->message);
        return FALSE;
    }
    g_byte_array_remove_range(socket_to_send, 0, bw);
    if ( bw == 0){
        return FALSE;
    }

    return TRUE;
}

/**
 * Write an int on two hex bytes,
 * or write twohex bytes into one int
 * Used to get/set length according to vpcd's protocol
 **/
void convert_byte_hex(int *hex, uint8_t *part1, uint8_t *part2, convmode mode){
    switch(mode){
        case HEX2BYTES:
            //Cut one hex variable into two bytes
            *part1 = (*hex >> 8) & 0xff;
            *part2 = *hex & 0xff;
            break;
        case BYTES2HEX:
            //Merge two bytes into one hex variable
            *hex = (*part1 << 8) ;
            *hex = *hex | *part2 ;
            break;
    }
}

/**
 * Power off the card
 **/
gboolean make_reply_poweroff(void){
    VReader *r = vreader_get_reader_by_name(reader_name);
    VReaderStatus status = vreader_power_off(r);

    vreader_free(r);
    if(status != VREADER_OK){
        printf("Error powering off card\n");
        return FALSE;
    }
    return TRUE;
}
/**
 * Power on the card
 **/
gboolean make_reply_poweron(void){
    VReader *r = vreader_get_reader_by_name(reader_name);
    VReaderStatus status = vreader_power_on(r, NULL, NULL);

    vreader_free(r);
    if(status != VREADER_OK){
        printf("Error powering on card\n");
        return FALSE;
    }
    return TRUE;
}
/**
 * Responds to apdu. Format the reponse made by libcacard according to 
 * vpcd's protocol
 **/
gboolean make_reply_apdu(uint8_t *buffer, int send_buff_len){
    int receive_buf_len = APDUBufSize;
    uint8_t part1, part2, receive_buff[APDUBufSize];
    VReaderStatus status; 
    /* get by name ref */  
    VReader *r = vreader_get_reader_by_name(reader_name);
    g_mutex_lock(&socket_to_send_lock);

    g_byte_array_remove_range(socket_to_send,0,socket_to_send->len);
    if (r != NULL){
        status = vreader_xfr_bytes(r, buffer, send_buff_len, receive_buff, &receive_buf_len);
    }else{
        printf("Error getting reader\n");
        return FALSE;
    }
    if(status != VREADER_OK){
        printf("xfr apdu failed\n");
        vreader_free(r); 
        return FALSE;
    }
    print_apdu(receive_buff, receive_buf_len);
    //Format reply with the first two bytes for length and then the data:
    convert_byte_hex(&receive_buf_len, &part1, &part2, HEX2BYTES);
    g_byte_array_append(socket_to_send,&part1, 1);
    g_byte_array_append(socket_to_send,&part2, 1);
    g_byte_array_append(socket_to_send,receive_buff,receive_buf_len);

    gboolean isSent = do_socket_send(channel_socket, G_IO_OUT, NULL);
    g_mutex_unlock(&socket_to_send_lock);
    vreader_free(r);
    return isSent;
}

/**
 * Respond to Get ATR message
 **/
gboolean make_reply_atr(void){
    g_mutex_lock(&socket_to_send_lock);
    uint8_t *atr;
    uint8_t *reply;
    gboolean isSent = FALSE;
    VReader *r = vreader_get_reader_by_name(reader_name);
    if( r  == NULL){
        printf("Reader pb\n");
        g_mutex_unlock(&socket_to_send_lock);
        return isSent;
    }
    int atr_length = MAX_ATR_LEN;
    atr = calloc(MAX_ATR_LEN,sizeof(uint8_t));
    
    vcard_emul_get_atr(NULL, atr, &atr_length);
    reply = calloc(atr_length+2,sizeof(uint8_t));
    //Format reply with the first two bytes for length and then the data:
    convert_byte_hex(&atr_length, &reply[0], &reply[1],HEX2BYTES);
    for(int i = 0; i < atr_length; i++){
        reply[i+2] = atr[i];
    }
    g_byte_array_remove_range(socket_to_send,0,socket_to_send->len);
    g_byte_array_append(socket_to_send, reply, atr_length+2);
    isSent = do_socket_send(channel_socket, G_IO_OUT, NULL);
    g_mutex_unlock(&socket_to_send_lock);
    free(atr);
    free(reply);
    vreader_free(r);
    return isSent;
}


/** vpcd communicates over a socked with vpicc usually on port 0x8C7B 
 *  (configurably via /etc/reader.conf.d/vpcd). 
 *  So you can connect virtually any program to the virtual smart card reader, 
 *  as long as you respect the following protocol:
 _____________________________________________________________
 |    vpcd                      |     vpicc                    |
 |______________________________|______________________________|
 |Length 	|   Command 	    |  Length 	 |   Response      |
 |0x00 0x01 |  0x00 (Power Off) |   N/A      |   (No Response) |
 |0x00 0x01 |  0x01 (Power On)  |	  N/A    |   (No Response) |
 |0x00 0x01 |  0x02 (Reset) 	|	  N/A    |   (No Response) |
 |0x00 0x01 |  0x04 (Get ATR)   |  0xXX 0xXX |   (ATR)         |
 |0xXX 0xXX |  (APDU) 	        |  0xXX 0xXX |   (R-APDU)      |
 |__________|___________________|____________|_________________|

 *  The communication is initiated by vpcd. First the length of the data (in network byte order,
 *  i.e. big endian) is sent followed by the data itself.
 **/

static gboolean do_socket_read(GIOChannel *source, GIOCondition condition, gpointer data)
{
    GError *error = NULL;
    uint8_t *buffer = calloc(APDUBufSize, sizeof(uint8_t));
    gsize wasRead = 0;
    gsize toRead = 2; // The first two bytes contain the length of the following message
    int rcvLength = 0;
    gboolean isOk = TRUE;
    static gboolean poweredOff = FALSE;

    if (condition & G_IO_HUP)
        g_error ("Write end of pipe died!\n");
    g_io_channel_read_chars(source,(gchar *) buffer, toRead, &wasRead, &error); 
    if (error != NULL){
        g_error("error while reading: %s", error->message);
        free(buffer);
        return FALSE;
    }

    if(wasRead == 2){
        convert_byte_hex(&rcvLength, &buffer[0], &buffer[1], BYTES2HEX);

        printf("rcvLength = %i\n wasRead = %li\n",rcvLength, wasRead);
        g_io_channel_read_chars(source, (gchar *) buffer, rcvLength, &wasRead, &error);
        printf("second part rcvLength = %i\n wasRead = %li\n",rcvLength, wasRead);
        if(wasRead == VPCD_CTRL_LEN){
            int code = buffer[0];
            printf("received ctrl code: %i\n",code);
            switch(code){
                case VPCD_CTRL_ON:
                    //TODO POWER ON
                    printf("Power on requested by vpcd\n");
                    printf("Powering up card\n");
                    poweredOff = !make_reply_poweron();
                    break;
                case VPCD_CTRL_OFF:
                    //TODO POWER OFF
                    printf("Power off requested by vpcd\n");
                    printf("powered off card\n");
                    poweredOff = make_reply_poweroff();
                    break;
                case VPCD_CTRL_RESET:
                    //TODO RESET
                    printf("Reset requested by vpcd\n");
                    break;
                case VPCD_CTRL_ATR:
                    printf("ATR requested by vpcd\n");
                    if(make_reply_atr()){
                        printf(" card answered to reset\n");
                        isOk = TRUE;
                    }else{
                        printf("Failed to get ATR\n");
                    }
                    break;
                default:
                    printf("Non recognized code\n");
            }
        }else{
            printf("Received APDU of size %i:\n",rcvLength);
            print_apdu(buffer, rcvLength);
            if(make_reply_apdu(buffer, rcvLength)){
                printf(" card answered to APDU\n");
                isOk = TRUE;
            }else{
                printf("Failed to answer to APDU\n");
            }
        }
    }else if( wasRead == 0){
        return FALSE;
    }
    g_io_channel_flush(channel_socket, &error);
    if(error != NULL){
        g_error("Error while flushing: %s", error->message);
    }
    free(buffer);
    return isOk;
}


int main(int argc, char* argv[])
{
    VReader *r = NULL;
    VCardEmulError ret;
    int code = 0;
    SOCKET sock;
    uint16_t port = VPCDPORT;
    socket_to_send = g_byte_array_new();

    loop = g_main_loop_new(NULL, TRUE);

    /**
     **************** vCAC INIT *******************
     **/
    /* init_cacard needs a running main loop to start the reader events thread */
    ret = init_cacard();
    if(ret != VCARD_EMUL_OK)
        return EXIT_FAILURE;
    /**
     **************** CONNECTION TO VPCD *******************
     **/
    /* Connecting to VPCD and creating an IO channel to manage the socket */
    sock = connectsock(hostname,port);
    
    if(sock == -1){
        printf("Connect sock error\n");
        return EXIT_FAILURE;
    }

    channel_socket = g_io_channel_unix_new(sock);
    g_io_channel_set_encoding(channel_socket, NULL, NULL);
    g_io_channel_set_buffered(channel_socket, FALSE);

    /* Adding watches to the channel.*/
    socket_tag_read = g_io_add_watch(channel_socket, G_IO_IN | G_IO_HUP, do_socket_read, NULL);
    if(!socket_tag_read)
        g_error("Error creating send watch\n");

    socket_tag_send = g_io_add_watch(channel_socket, G_IO_OUT | G_IO_HUP, do_socket_send, NULL);
    if(!socket_tag_send) 
        g_error("Error creating send watch\n");

    g_main_loop_run(loop); //explicit

    /**
     **************** CLEAN UP  ******************
     **/
    printf("*******\tCleaning up\t*******\n\n");
    r = vreader_get_reader_by_name(reader_name);

    /* This probably supposed to be a event that terminates the loop */
    vevent_queue_vevent(vevent_new(VEVENT_LAST, r, NULL));

    /* join */
    g_thread_join(thread);

    /* Clean up */
    if (r) /*if /remove didn't run */
        vreader_free(r);

    g_io_channel_shutdown(channel_socket, TRUE, NULL);
    g_io_channel_unref(channel_socket);
    closesocket(sock);
    g_main_loop_unref(loop);
    g_byte_array_free(socket_to_send, TRUE);

    return code;
}

/* vim: set ts=4 sw=4 tw=0 noet expandtab: */
