#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <libcacard.h>

#include "connection.h"
#include "glib-compat.h"

#define ARGS "db=\"sql:%s\" use_hw=removable" //no need for soft options with use_hw=removable
#define APDUBufSize 270
#define PIN 77777777


static GMainLoop *loop;
static GThread *thread;
static guint nreaders;
static GMutex mutex;
static GCond cond;
static const char hostname[] = "/dev/null";

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

static gboolean socket_prepare_sending(gpointer user_data)
{
    update_socket_watch();

    return FALSE;
}

static gboolean do_socket(GIOChannel *source,
          GIOCondition condition,
          gpointer data)
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

static gboolean do_command(GIOChannel *source, GIOCondition condition, gpointer data)
{
    char *string;
    VCardEmulError error;
    static unsigned int default_reader_id;
    unsigned int reader_id;
    VReader *reader = NULL;
    GError *err = NULL;

    g_assert(condition & G_IO_IN);

    reader_id = default_reader_id;
    g_io_channel_read_line(source, &string, NULL, NULL, &err);
    if (err != NULL) {
        g_error("Error while reading command: %s", err->message);
    }

    if (string != NULL) {
        if (strncmp(string, "exit", 4) == 0) {
            /* remove all the readers */
            VReaderList *list = vreader_get_reader_list();
            VReaderListEntry *reader_entry;
            printf("Active Readers:\n");
            for (reader_entry = vreader_list_get_first(list); reader_entry;
                    reader_entry = vreader_list_get_next(reader_entry)) {
                VReader *r = vreader_list_get_reader(reader_entry);
                vreader_id_t id;
                id = vreader_get_id(r);
                if (id == (vreader_id_t)-1) {
                    continue;
                }
                /* be nice and signal card removal first (qemu probably should
                 * do this itself) */
                if (vreader_card_is_present(r) == VREADER_OK) {
                    //TODO remove card
                }
                // TODO Remove reader
            }
            exit(0);
        } else if (strncmp(string, "insert", 6) == 0) {
            if (string[6] == ' ') {
                reader_id = get_id_from_string(&string[7], reader_id);
            }
            reader = vreader_get_reader_by_id(reader_id);
            if (reader != NULL) {
                error = vcard_emul_force_card_insert(reader);
                printf("insert %s, returned %d\n",
                        vreader_get_name(reader), error);
            } else {
                printf("no reader by id %u found\n", reader_id);
            }
        } else if (strncmp(string, "remove", 6) == 0) {
            if (string[6] == ' ') {
                reader_id = get_id_from_string(&string[7], reader_id);
            }
            reader = vreader_get_reader_by_id(reader_id);
            if (reader != NULL) {
                error = vcard_emul_force_card_remove(reader);
                printf("remove %s, returned %d\n",
                        vreader_get_name(reader), error);
            } else {
                printf("no reader by id %u found\n", reader_id);
            }
        } else if (strncmp(string, "select", 6) == 0) {
            if (string[6] == ' ') {
                reader_id = get_id_from_string(&string[7],
                        VSCARD_UNDEFINED_READER_ID);
            }
            if (reader_id != VSCARD_UNDEFINED_READER_ID) {
                reader = vreader_get_reader_by_id(reader_id);
            }
            if (reader) {
                printf("Selecting reader %u, %s\n", reader_id,
                        vreader_get_name(reader));
                default_reader_id = reader_id;
            } else {
                printf("Reader with id %u not found\n", reader_id);
            }
        } //TODO DEBUG LEVEL   
        else if (strncmp(string, "list", 4) == 0) {

            VReaderList *list = vreader_get_reader_list();
            VReaderListEntry *reader_entry;
            printf("Active Readers:\n");
            for (reader_entry = vreader_list_get_first(list); reader_entry;
                    reader_entry = vreader_list_get_next(reader_entry)) {
                VReader *r = vreader_list_get_reader(reader_entry);
                vreader_id_t id;
                id = vreader_get_id(r);
                if (id == (vreader_id_t)-1) {
                    continue;
                }
                printf("%3u %s %s\n", id,
                        vreader_card_is_present(r) == VREADER_OK ?
                        "CARD_PRESENT" : "            ",
                        vreader_get_name(r));
            }
            printf("Inactive Readers:\n");
            for (reader_entry = vreader_list_get_first(list); reader_entry;
                    reader_entry = vreader_list_get_next(reader_entry)) {
                VReader *r = vreader_list_get_reader(reader_entry);
                vreader_id_t id;
                id = vreader_get_id(reader);
                if (id != (vreader_id_t)-1) {
                    continue;
                }

                printf("INA %s %s\n",
                        vreader_card_is_present(r) == VREADER_OK ?
                        "CARD_PRESENT" : "            ",
                        vreader_get_name(r));
            }
            vreader_list_delete(list);
        } else if (*string != 0) {
            printf("valid commands:\n");
            printf("insert [reader_id]\n");
            printf("remove [reader_id]\n");
            printf("select reader_id\n");
            printf("list\n");
            printf("debug [level]\n");
            printf("exit\n");
        }
    }
    vreader_free(reader);
    printf("> ");
    fflush(stdout);

    return TRUE;
}


int main(int argc, char* argv[])
{
    VReader *r;
    VCardEmulError ret;
    int code = 0;
    SOCKET sock;
    uint16_t port = VPCDPORT;

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

    g_main_loop_unref(loop);
    return code;
}

/* vim: set ts=4 sw=4 tw=0 noet expandtab: */
