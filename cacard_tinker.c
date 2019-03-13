#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <libcacard.h>
#include <glib.h>

#define ARGS "db=\"sql:%s\" use_hw=no soft=(,Test,CAC,,cert1,cert2,cert3)"

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

    if (event != NULL)
    {
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
        printf("reader insert");
      case VEVENT_READER_REMOVE:
        printf("reader remove");
      case VEVENT_CARD_INSERT:
        printf("Card insert");
      case VEVENT_CARD_REMOVE:
        printf("Card remove");
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


int main(int argc, char* argv[])
{
  // GIOChannel *channel;
  char *vpcd_host;
  char *vpcd_port;
  char* dbdir = g_build_filename("db",NULL);
  char *args = g_strdup_printf(ARGS,dbdir);
  VCardEmulOptions *command_line_options = NULL;
  VCardEmulError ret;


  loop = g_main_loop_new(NULL, TRUE);
  /* TODO
   * 
   * Setup the card with appropriate certs, options, etc.
   *
   * */
  // Start the event thread
  thread = g_thread_new("tinker/events", events_thread, NULL);

  command_line_options = vcard_emul_options(args);
  ret = vcard_emul_init(command_line_options);
  if(ret != VCARD_EMUL_OK){
    return EXIT_FAILURE;
  }
  else{
    printf("Init ok with options \"%s\"\n", args);
  }
  return EXIT_SUCCESS;
}
