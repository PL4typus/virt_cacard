#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <libcacard.h>
#include <glib.h>


#define ARGS "db=\"sql:%s\" use_hw=removable" //no need for soft options with use_hw=removable
#define APDUBufSize 270
#define PIN 77777777


static GMainLoop *loop;
static GThread *thread;
static guint nreaders;
static GMutex mutex;
static GCond cond;


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


static gboolean transfer_test(void){
    VReader *reader = vreader_get_reader_by_id(0);

    VReaderStatus status;
    int RecvLength = APDUBufSize; 
    uint8_t RecvBuffer[APDUBufSize];
    uint8_t SendBuffer[] = {
        /* Select Applet that is not there */
        0x00, 0xa4, 0x04, 0x00, 0x07, 0x62, 0x76, 0x01, 0xff, 0x00, 0x00, 0x00,
    };

    if(reader != NULL){
        status = vreader_xfr_bytes(reader,
                SendBuffer, sizeof(SendBuffer),
                RecvBuffer, &RecvLength);
        vreader_free(reader); /* get by id ref */

        if(status == VREADER_OK) return TRUE;
    }
    return FALSE;
}

static int test_list(void)
{
    VReaderList *list = vreader_get_reader_list();
    VReaderListEntry *reader_entry;
    int cards = 0;

    for (reader_entry = vreader_list_get_first(list); reader_entry;
            reader_entry = vreader_list_get_next(reader_entry)) {
        VReader *r = vreader_list_get_reader(reader_entry);
        vreader_id_t id;
        id = vreader_get_id(r);
        printf("Readers: %s\tid:%d\n",vreader_get_name(r),id);
        if (vreader_card_is_present(r) == VREADER_OK) {
            cards++;
        }
        vreader_free(r);
    }
    vreader_list_delete(list);
    return cards;
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
    int cards;
    GIOChannel *channel_stdin;

    loop = g_main_loop_new(NULL, TRUE);
    //Initialize card with the correct set of options
    ret = init_cacard();
    if(ret != VCARD_EMUL_OK) return EXIT_FAILURE;
    //Test if we can get a list of readers, prints their names, id and number of connected cards
    cards = test_list();
    printf("# of cards: %i\n",cards);

    //Transfer a few bytes to the card
    if(!transfer_test()){
        printf("Failed transfer\n");
    }
    printf("> ");
    fflush(stdout);

    channel_stdin = g_io_channel_unix_new(STDIN_FILENO);
    g_io_add_watch(channel_stdin, G_IO_IN, do_command, NULL);
    g_main_loop_run(loop);

    g_io_channel_unref(channel_stdin);
    // Start of the cleaning up part
    r = vreader_get_reader_by_id(0);
    
    /* This probably supposed to be a event that terminates the loop */
    vevent_queue_vevent(vevent_new(VEVENT_LAST, r, NULL));

    /* join */
    g_thread_join(thread);

    /* Clean up */
    vreader_free(r);

    g_main_loop_unref(loop);
    return EXIT_SUCCESS;
}



/* vim: set ts=4 sw=4 tw=0 noet expandtab: */
