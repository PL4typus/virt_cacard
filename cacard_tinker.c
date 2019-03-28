/*
 * cacard_tinker.c A program making use of libcacard and vpcd
 * to present a virtual CACard through PC/SC API
 *
 * Copyright (C) 2019 Red Hat
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
#define VPCD_CTRL_LEN 	1
#define VPCD_CTRL_OFF   0
#define VPCD_CTRL_ON    1
#define VPCD_CTRL_RESET 2
#define VPCD_CTRL_ATR	4
#define APDUBufSize 270
#define MAX_ATR_LEN 40
#define PIN 77777777


static GMainLoop *loop;
static GThread *thread;
static guint nreaders;
static GMutex mutex;
static GCond cond;
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
            case VEVENT_READER_REMOVE:
            case VEVENT_CARD_INSERT:
            case VEVENT_CARD_REMOVE:
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
        r = vreader_get_reader_by_id(0);

        if(r == NULL){
            printf("No readers\n");
            ret = VCARD_EMUL_FAIL;
        }else{
            vreader_free(r); /* get by name ref */

            g_mutex_lock(&mutex);
            while (nreaders <= 1)
                g_cond_wait(&cond, &mutex);
            g_free(args);
            g_free(dbdir);
        }
    }
    return ret;
}

static unsigned int get_id_from_string(char *string, unsigned int default_id)
{
    unsigned int id = atoi(string);

    /* don't accidentally swith to zero because no numbers have been supplied */
    if ((id == 0) && *string != '0') {
        return default_id;
    }
    return id;
}

static GIOChannel *channel_socket;
static GByteArray *socket_to_send;
static CompatGMutex socket_to_send_lock;
static guint socket_tag;

static void
update_socket_watch(void);

static gboolean do_socket_send(GIOChannel *source, GIOCondition condition, gpointer data)
{
    gsize bw;
    GError *err = NULL;

    g_return_val_if_fail(socket_to_send->len != 0, FALSE);
    g_return_val_if_fail(condition & G_IO_OUT, FALSE);

    g_io_channel_write_chars(channel_socket,
            (gchar *)socket_to_send->data, socket_to_send->len, &bw, &err);
    if (err != NULL) {
        g_error("Error while sending socket %s", err->message);
        return FALSE;
    }
    g_byte_array_remove_range(socket_to_send, 0, bw);

    if (socket_to_send->len == 0) {
        update_socket_watch();
        return FALSE;
    }
    return TRUE;
}

static char* reader_name = "SoftHSM slot ID 0x4590c6e6";

void atr_reply(){
    VReader *r = vreader_get_reader_by_name(reader_name);
    uint8_t atr[MAX_ATR_LEN];
    int atr_length = MAX_ATR_LEN;
    vreader_power_on(r, atr, &atr_length);
    for(int i = 0; i < atr_length; i++){
        printf("%x ",atr[i]);
    }
    printf("\n");
}

static gboolean do_socket_read(GIOChannel *source, GIOCondition condition, gpointer data)
{
    //TODO: socket read
    GError *error = NULL;
    uint8_t *buffer;
    gsize wasRead, toRead;
    toRead = APDUBufSize;
    uint16_t rcvLength;

    g_io_channel_read_chars(source, (gchar*)data, toRead, &wasRead, &error); 
    if (error != NULL){
        g_error("error while reading: %s", error->message);
        return FALSE;
    }
    buffer = data;
    /** vpcd communicates over a socked with vpicc usually on port 0x8C7B 
     *  (configurably via /etc/reader.conf.d/vpcd). 
     *  So you can connect virtually any program to the virtual smart card reader, 
     *  as long as you respect the following protocol:
    __ ___________________________________________________________
   |    vpcd                      |     vpicc                     |
   |______________________________|_______________________________|
   |Length 	  |   Command 	      |  Length 	|   Response      |
   |0x00 0x01 |	  0x00 (Power Off)|   N/A       |   (No Response) |
   |0x00 0x01 |	  0x01 (Power On) |	  N/A       |   (No Response) |
   |0x00 0x01 |	  0x02 (Reset) 	  |	  N/A       |   (No Response) |
   |0x00 0x01 |	  0x04 (Get ATR)  |  0xXX 0xXX 	|   (ATR)         |
   |0xXX 0xXX |	  (APDU) 	      |  0xXX 0xXX  |   (R-APDU)      |
   |__________|___________________|_____________|_________________|

     *  The communication is initiated by vpcd. First the length of the data (in network byte order,
     *  i.e. big endian) is sent followed by the data itself.
     **/
    
    rcvLength = (*buffer << 8);
    buffer++;
    rcvLength = rcvLength | *buffer;
    buffer++;
    if(rcvLength == VPCD_CTRL_LEN){
        int code = *buffer;
        printf("received: %i\n",code);
        switch(code){
            case VPCD_CTRL_ON:
                //TODO POWER ON
                printf("Power on requested by vpcd\n");
                break;
            case VPCD_CTRL_OFF:
                //TODO POWER OFF
                printf("Power off requested by vpcd\n");
                break;
            case VPCD_CTRL_RESET:
                //TODO RESET
                printf("Reset requested by vpcd\n");
                break;
            case VPCD_CTRL_ATR:
                //TODO ATR
                printf("ATR requested by vpcd\n");
                atr_reply();
                break;
            default:
                printf("Non recognized code\n");
        }
    }else{
        //TODO PROCESS APDU
        printf("Received APDU of size %i:\n%s\n",rcvLength,buffer);
    }
    return TRUE;
}


static gboolean socket_prepare_sending(gpointer user_data)
{
    update_socket_watch();

    return FALSE;
}

static gboolean do_socket(GIOChannel *source, GIOCondition condition,gpointer data)
{
    /* not sure if two watches work well with a single win32 sources */
    if (condition & G_IO_OUT) {
        if (!do_socket_send(source, condition, data)) {
            return FALSE;
        }
    }

    if (condition & G_IO_IN) {
        if (!do_socket_read(source, condition, data)) {
            return FALSE;
        }
    }

    return TRUE;
}

static void update_socket_watch(void)
{
    gboolean out = socket_to_send->len > 0;

    if (socket_tag != 0) {
        g_source_remove(socket_tag);
    }

    socket_tag = g_io_add_watch(channel_socket,
            G_IO_IN | (out ? G_IO_OUT : 0), do_socket, NULL);
}


int main(int argc, char* argv[])
{
    VReader *r;
    VCardEmulError ret;
    int code = 0;
    SOCKET sock;
    uint16_t port = VPCDPORT;
    gpointer *data = malloc(APDUBufSize*sizeof(uint8_t));

    loop = g_main_loop_new(NULL, TRUE);
    /**
     **************** vCAC INIT *******************
     **/
    ret = init_cacard();
    if(ret != VCARD_EMUL_OK) return EXIT_FAILURE;

    sock = connectsock(hostname,port);
    channel_socket = g_io_channel_unix_new(sock);
    g_io_channel_set_encoding(channel_socket, NULL, NULL);
    /* we buffer ourself for thread safety reasons */
    g_io_channel_set_buffered(channel_socket, FALSE);
    do_socket_read(channel_socket, G_IO_IN, data);

    /**
     **************** Clean up  ******************
     **/
    r = vreader_get_reader_by_id(0);

    /* This probably supposed to be a event that terminates the loop */
    vevent_queue_vevent(vevent_new(VEVENT_LAST, r, NULL));

    /* join */
    g_thread_join(thread);

    /* Clean up */
    vreader_free(r);

    g_io_channel_shutdown(channel_socket, TRUE, NULL);
    g_io_channel_unref(channel_socket);
    g_main_loop_unref(loop);
    return code;
}

/* vim: set ts=4 sw=4 tw=0 noet expandtab: */
