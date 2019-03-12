#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <libcacard.h>
#include <glib.h>

#define ARGS "db=\"sql:%s\" use_hw=no soft=(,Test,CAC,,cert1,cert2,cert3)"

int main(int argc, char* argv[])
{
  GMainLoop *loop;
  GIOChannel *channel;
  char *vpcd_host;
  char *vpcd_port;
  char* dbdir = g_build_filename("db",NULL);
  char *args = g_strdup_printf(ARGS,dbdir);
  VCardEmulOptions *command_line_options = NULL;
  VCardEmulError ret;
  /* TODO
   * 
   * Setup the card with appropriate certs, options, etc.
   *
   * */
  command_line_options = vcard_emul_options(args);
  ret = vcard_emul_init(command_line_options);
  if(ret != VCARD_EMUL_OK){
    return -1;
  }
  else{
    printf("Init ok with options \"%s\"\n", args);
  }
  /* TODO
   * 
   * set up the connection to the VPCD, then wait for events
   * 
   * */
  return 0;
}
