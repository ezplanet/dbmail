/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * This is the dbmail housekeeping program. 
 *	It checks the integrity of the database and does a cleanup of all
 *	deleted messages. 
 */

#include "maintenance.h"
#include "db.h"
#include "debug.h"
#include "config.h"
#include "list.h"
#include "debug.h"
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

char *configFile = "dbmail.conf";

/* set up database login data */
extern field_t _db_host;
extern field_t _db_db;
extern field_t _db_user;
extern field_t _db_pass;


void find_time(char *timestr, const char *timespec);


int main(int argc, char *argv[])
{
  int should_fix = 0, check_integrity = 0, check_iplog = 0;
  int check_null_messages=0;
  int show_help=0, purge_deleted=0, set_deleted=0;
  int vacuum_db = 0;
  int do_nothing=1;
  struct list sysItems;

  time_t start,stop;

  char timespec[LEN],timestr[LEN];
  int opt;
  struct list lostlist;
  struct element *el;
  u64_t id;

  u64_t deleted_messages;
  u64_t messages_set_to_delete;
    
  openlog(PNAME, LOG_PID, LOG_MAIL);

  ReadConfig("DBMAIL", "dbmail.conf", &sysItems);
  SetTraceLevel(&sysItems);
  GetDBParams(_db_host, _db_db, _db_user, _db_pass, &sysItems);
	
  setvbuf(stdout,0,_IONBF,0);
  printf ("*** dbmail-maintenance ***\n");
	
  /* get options */
  opterr = 0; /* suppress error message from getopt() */
  while ((opt = getopt(argc, argv, "cfinl:phd")) != -1)
    {
      switch (opt)
	{
	case 'c':
	  vacuum_db = 1;
	  do_nothing = 0;
	  break;

	case 'h':
	  show_help = 1;
	  do_nothing = 0;
	  break;

	case 'p':
	  purge_deleted = 1;
	  do_nothing = 0;
	  break;

	case 'd':
	  set_deleted = 1;
	  do_nothing = 0;
	  break;

	case 'f':
	  check_integrity = 1;
	  should_fix = 1;
	  do_nothing = 0;
	  break;

	case 'i':
	  check_integrity = 1;
	  do_nothing = 0;
	  break;

	case 'n':
	  check_null_messages = 1;
	  do_nothing = 0;
	  break;

	case 'l':
	  check_iplog = 1;
	  do_nothing = 0;
	  if (optarg)
	    strncpy(timespec, optarg, LEN);
	  else
	    timespec[0] = 0;

	  timespec[LEN] = 0;
	  break;

	default:
	  /*printf("unrecognized option [%c], continuing...\n",optopt);*/
	  break;
	}
    }

  if (show_help)
    {
      printf("\ndbmail maintenance utility\n\n");
      printf("Performs maintenance tasks on the dbmail-databases\n");
      printf("Use: dbmail-maintenance -[cfiphdl]\n");
      printf("See the man page for more info\n\n");
      return 0;
    }


  if (do_nothing)
    {
      printf("Ok. Nothing requested, nothing done. "
	     "Try adding a command-line option to perform maintenance.\n");
      return 0;
    }

  printf ("Opening connection to the database... ");

  if (db_connect()==-1)
    {
      printf ("Failed. An error occured. Please check log.\n");
      return -1;
    }

  printf ("Ok. Connected\n");

  if (purge_deleted)
    {
      printf ("Deleting messages with DELETE status... ");
      deleted_messages=db_deleted_purge();
      if (deleted_messages==-1)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
	  return -1;
	}
      printf ("Ok. [%llu] messages deleted.\n",deleted_messages);
    }
	

  if (set_deleted)
    {
      printf ("Setting DELETE status for deleted messages... ");
      messages_set_to_delete= db_set_deleted ();
      if (messages_set_to_delete==-1)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
	  return -1;
	}
      printf ("Ok. [%llu] messages set for deletion.\n",messages_set_to_delete);
    }

  if (check_null_messages)
    {
      printf ("Now checking DBMAIL for NULL messages.. ");
      time(&start);

      if (db_icheck_null_messages(&lostlist) < 0)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
	  return -1;
	}
    
      if (lostlist.total_nodes > 0)
	{
	  printf ("Ok. Found [%ld] null messages:\n", lostlist.total_nodes);
      
	  el = lostlist.start;
	  while (el)
	    {
	      id = *((u64_t*)el->data);
	      if (db_set_message_status(id, 6) < 0)
		printf("Warning: could not set message status #%llu. Check log.\n", id);
	      else
		printf("%llu (removed from dbase)\n", id);

	      el = el->nextnode;
	    }
        
	  list_freelist(&lostlist.start);

	  printf ("\n");
	}
      else 
	printf ("Ok. Found 0 NULL messages.\n");

      time(&stop);
      printf("--- checking block integrity took %lu seconds\n", stop-start);
      fprintf(stderr, "--- checking block integrity took %lu seconds\n", stop-start);
    }

  if (check_integrity)
    {
      printf ("Now checking DBMAIL messageblocks integrity.. ");
      time(&start);

      /* this is what we do:
       * First we're checking for loose messageblocks
       * Secondly we're chekcing for loose messages
       * Third we're checking for loose mailboxes 
       */

      /* first part */
      if (db_icheck_messageblks(&lostlist) < 0)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
	  return -1;
	}
    
      if (lostlist.total_nodes > 0)
	{
	  printf ("Ok. Found [%ld] unconnected messageblks:\n", lostlist.total_nodes);
      
	  el = lostlist.start;
	  while (el)
	    {
	      id = *((u64_t*)el->data);
	      if (should_fix == 0)
		printf("%llu ", id);
	      else
		{
		  if (db_delete_messageblk(id) < 0)
		    printf("Warning: could not delete messageblock #%llu. Check log.\n", id);
		  else
		    printf("%llu (removed from dbase)\n",id);
		}

	      el = el->nextnode;
	    }
        
	  list_freelist(&lostlist.start);

	  printf ("\n");
	  if (should_fix == 0)
	    {
	      printf("Try running dbmail-maintenance with the '-f' option "
		     "in order to fix these problems\n\n");
	    }
	}
      else 
	printf ("Ok. Found 0 unconnected messageblks.\n");


      time(&stop);
      printf("--- checking block integrity took %lu seconds\n", stop-start);
      fprintf(stderr, "--- checking block integrity took %lu seconds\n", stop-start);
      
      /* second part */
      start = stop;
      printf ("Now checking DBMAIL message integrity.. ");

      if (db_icheck_messages(&lostlist) < 0)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
	  return -1;
	}
    
      if (lostlist.total_nodes > 0)
	{
	  printf ("Ok. Found [%ld] unconnected messages:\n", lostlist.total_nodes);
      
	  el = lostlist.start;
	  while (el)
	    {
	      id = *((u64_t*)el->data);

	      if (should_fix == 0)
		printf("%llu ", id);
	      else
		{
		  if (db_delete_message(id) < 0)
		    printf("Warning: could not delete message #%llu. Check log.\n", id);
		  else
		    printf("%llu (removed from dbase)\n", id);
		}

	      el = el->nextnode;
	    }
        
	  printf ("\n");
	  if (should_fix == 0)
	    {
	      printf("Try running dbmail-maintenance with the '-f' option "
		     "in order to fix these problems\n\n");
	    }
	  list_freelist(&lostlist.start);

	}
      else 
	printf ("Ok. Found 0 unconnected messages.\n");
        
      time(&stop);
      printf("--- checking message integrity took %lu seconds\n", stop-start);
      fprintf(stderr, "--- checking message integrity took %lu seconds\n", stop-start);


      /* third part */
      printf ("Now checking DBMAIL mailbox integrity.. ");
      start = stop;

      if (db_icheck_mailboxes(&lostlist) < 0)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
	  return -1;
	}
    
      if (lostlist.total_nodes)
	{
	  printf ("Ok. Found [%ld] unconnected mailboxes:\n", lostlist.total_nodes);
      
	  el = lostlist.start;
	  while (el)
	    {
	      id = *((u64_t*)el->data);

	      if (should_fix == 0)
		printf("%llu ", id);
	      else
		{
		  if (db_delete_mailbox(id, 0) < 0)
		    printf("Warning: could not delete mailbox #%llu. Check log.\n", id);
		  else
		    printf("%llu (removed from dbase)\n",id);
		}

	      el = el->nextnode;
	    }
        
	  printf ("\n");
	  if (should_fix == 0)
	    {
	      printf("Try running dbmail-maintenance with the '-f' option "
		     "in order to fix these problems\n\n");
	    }

	  list_freelist(&lostlist.start);
	}
      else 
	printf ("Ok. Found 0 unconnected mailboxes.\n");
        
      time(&stop);
      printf("--- checking mailbox integrity took %lu seconds\n", stop-start);
      fprintf(stderr, "--- checking mailbox integrity took %lu seconds\n", stop-start);
   }

  if (check_iplog)
    {
      find_time(timestr, timespec);
      printf("Cleaning up IP log... ");

      if (timestr[0] == 0)
	{
	  printf("Failed. Invalid argument [%s] specified\n",timespec);
	  db_disconnect();
	  return -1;
	}

      if (db_cleanup_iplog(timestr) < 0)
	{
	  printf("Failed. Please check the log.\n");
	  db_disconnect();
	  return -1;
	}
      
      printf("Ok. All entries before [%s] have been removed.\n",timestr);
    }
  
  if (vacuum_db)
    {
      printf("Cleaning up database structure... "); fflush(stdout);
      if (db_cleanup() < 0)
	{
	  printf("Failed. Please check the log.\n");
	  db_disconnect();
	  return -1;
	}
      
      printf("Ok. Database cleaned up.\n");
    }

  printf ("Maintenance done.\n\n");
        
  db_disconnect();
  return 0;
}



/* 
 * makes a date/time string: YYYY-MM-DD HH:mm:ss
 * based on current time minus timespec
 * timespec contains: <n>h<m>m for a timespan of n hours, m minutes
 * hours or minutes may be absent, not both
 *
 * upon error, timestr[0] = 0
 */
void find_time(char *timestr, const char *timespec)
{
  time_t td;
  struct tm tm;
  int min=-1,hour=-1;
  long tmp;
  char *end;

  time(&td);              /* get time */
 
  timestr[0] = 0;
  if (!timespec)
    return;

  /* find first num */
  tmp = strtol(timespec, &end, 10);
  if (!end)
    return;

  if (tmp < 0)
    return;

  switch (*end)
    {
    case 'h':
    case 'H':
      hour = tmp;
      break;
      
    case 'm':
    case 'M':
      hour = 0;
      min = tmp;
      if (end[1]) /* should end here */
	return;

      break;

    default:
      return;
    }


  /* find second num */
  if (timespec[end-timespec+1])
    {
      tmp = strtol(&timespec[end-timespec+1], &end, 10);
      if (end)
	{
	  if ((*end != 'm' && *end != 'M') || end[1])
	    return;

	  if (tmp < 0)
	    return;

	  if (min >= 0) /* already specified minutes */
	    return;

	  min = tmp;
	}
    }

  if (min < 0) 
    min = 0;

  /* adjust time */
  td -= (hour * 3600L + min * 60L);
  
  tm = *localtime(&td);   /* get components */
  strftime(timestr, LEN, "%G-%m-%d %H:%M:%S", &tm);

  return;
}





