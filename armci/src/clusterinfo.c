/* $Id: clusterinfo.c,v 1.16 2002-07-23 00:53:23 d3h325 Exp $ */
/****************************************************************************** 
* file:    cluster.c
* purpose: Determine cluster info i.e., number of machines and processes
*          running on each of them.
*
*******************************************************************************/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef unix
#include <unistd.h>
#endif
#include "message.h"
#include "armcip.h"

#ifdef WIN32
   /* this is where gethostbyname is declared */
#  include <winsock.h>
#endif

/* DEBUG_HACK enables to simulate cluster environment on a single workstation.
 * CLUSNODES is the number of processes assigned to each cluster node.
 * Must define NO_SHMMAX_SEARCH in shmem.c to prevent depleting shared memory
 * due to a gready shmem request by the master process on cluster node 0.
 */ 
#define DEBUG_HACK____
#define CLUSNODES 2

#define DEBUG  0
#define MAX_HOSTNAME 80
#define CHECK_NODE_NAMES 

/*  print info on how many cluster nodes detected */
#ifdef CLUSTER
#  define PRINT_CLUSTER_INFO 1
#else
#  define PRINT_CLUSTER_INFO 0
#endif

/*** stores cluster configuration ***/
armci_clus_t *armci_clus_info;

#ifdef HITACHI
#include <hmpp/nalloc.h>
# define GETHOSTNAME sr_gethostname
ndes_t _armci_group;

static int sr_gethostname(char *name, int len)
{
int no;
pid_t ppid;

   if(hmpp_nself (&_armci_group,&no,&ppid,0,NULL) <0)
     return -1;

   if(len<6)armci_die("len too small",len);
   if(no>1024)armci_die("expected node id <1024",no);
   sprintf(name,"n%d",no);
   return 0;
}
#else
# define GETHOSTNAME gethostname
#endif

static char* merge_names(char *name)
{
    int jump = 1, rem, to, from;
    int lenmes, lenbuf, curlen, totbuflen= armci_nproc*HOSTNAME_LEN;
    int len = strlen(name);
    char *work = malloc(totbuflen);

    if(!work)armci_die("armci: merge_names: malloc failed: ",totbuflen);

    strcpy(work, name); 
    curlen = len+1;

    /* prefix tree merges names in the order of process numbering in log(P)time
     * result = name_1//name_2//...//name_P-1
     */
    do {
       jump *= 2; rem = armci_me%jump;
       if(rem){
              to = armci_me - rem;
              armci_msg_snd(ARMCI_TAG, work, curlen, to);
              break;
       }else{
              from = armci_me + jump/2;
              if(from < armci_nproc){
                 lenbuf = totbuflen - curlen;
                 armci_msg_rcv(ARMCI_TAG, work+curlen, lenbuf, &lenmes, from);
                 curlen += lenmes;
              }
       }
    }while (jump < armci_nproc);
    return(work);
}


static void process_hostlist(char *names)
{
#ifdef CLUSTER

    int i, cluster=0;
    char *s,*master;
    int len, root=0;

    /******** inspect list of machine names to determine locality ********/
    if (armci_me==0){
     
      /* first find out how many cluster nodes we got */
      armci_nclus =1; s=master=names; 
      for(i=1; i < armci_nproc; i++){
        s += strlen(s)+1;
        if(strcmp(s,master)){
          /* we found a new machine name on the list */
          master = s;
          armci_nclus++;
/*          fprintf(stderr,"new name %s len =%d\n",master, strlen(master));*/

        }
      }

      /* allocate memory */ 
      armci_clus_info = (armci_clus_t*)malloc(armci_nclus*sizeof(armci_clus_t));
      if(!armci_clus_info)armci_die("malloc failed for clusinfo",armci_nclus);

      /* fill the data structure  -- go through the list again */ 
      s=names;
      master="*-"; /* impossible hostname */
      cluster =0;
      for(i=0; i < armci_nproc; i++){
        if(strcmp(s,master)){
          /* we found a new machine name on the list */
          master = s;
          armci_clus_info[cluster].nslave=1;
          armci_clus_info[cluster].master=i;
          strcpy(armci_clus_info[cluster].hostname, master); 

#ifdef    CHECK_NODE_NAMES
          /* need consecutive task id allocated on the same node
           * the current test only compares hostnames against first cluster */
          if(cluster) if(!strcmp(master,armci_clus_info[0].hostname)){
               /* we have seen that hostname before */
               fprintf(stderr, "\nIt appears that tasks allocated on the same");
               fprintf(stderr, " host machine do not have\n");
               fprintf(stderr, "consecutive message-passing IDs/numbers. ");
               fprintf(stderr,"This is not acceptable \nto the ARMCI library ");
               fprintf(stderr,"as it prevents SMP optimizations and would\n");
               fprintf(stderr,"lead to poor resource utilization.\n\n");
               fprintf(stderr,"Please contact your System Administrator ");
               fprintf(stderr,"or, if you can, modify the ");
#              if defined(MPI)
                 fprintf(stderr,"MPI");
#              elif defined(TCGMSG)
                 fprintf(stderr,"TCGMSG");
#              elif defined(PVM)
                 fprintf(stderr,"PVM");
#              endif
               fprintf(stderr,"\nmessage-passing job startup configuration.\n\n");
#ifdef HITACHI
               fprintf(stderr,"On Hitachi it can be done by setting environment variable MPIR_RANK_NO_ROUND, for example\n  setenv MPIR_RANK_NO_ROUND yes\n\n");
#endif
               sleep(1);
               armci_die("Cannot run: improper task to host mapping!",0); 
          }
#endif
          cluster++;

        }else{
          /* the process is still on the same host */
          armci_clus_info[cluster-1].nslave++;
        }
        s += strlen(s)+1;
      }

      if(armci_nclus != cluster)
         armci_die("inconsistency processing clusterinfo",armci_nclus);

    }
    /******** process 0 got all data                             ********/

   /* now broadcast locality info struct to all processes 
    * two steps are needed because of the unknown length of hostname list
    */
    len = sizeof(int);
    armci_msg_brdcst(&armci_nclus, len, root);

    if(armci_me){
      /* allocate memory */ 
      armci_clus_info = (armci_clus_t*)malloc(armci_nclus*sizeof(armci_clus_t));
      if(!armci_clus_info)armci_die("malloc failed for clusinfo",armci_nclus);
    }

    len = sizeof(armci_clus_t)*armci_nclus;
    armci_msg_brdcst(armci_clus_info, len, root);

    /******** all processes 0 got all data                         ********/

    /* now determine current cluster node id by comparing me to master */
    armci_clus_me = armci_nclus-1;
    for(i =0; i< armci_nclus-1; i++)
           if(armci_me < armci_clus_info[i+1].master){
              armci_clus_me=i;
              break;
           }
#else

    armci_clus_me=0;
    armci_nclus=1;
    armci_clus_info = (armci_clus_t*)malloc(armci_nclus*sizeof(armci_clus_t));
    if(!armci_clus_info)armci_die("malloc failed for clusinfo",armci_nclus);
    strcpy(armci_clus_info[0].hostname, names); 
    armci_clus_info[0].master=0;
    armci_clus_info[0].nslave=armci_nproc;
#endif

    armci_clus_first = armci_clus_info[armci_clus_me].master;
    armci_clus_last = armci_clus_first +armci_clus_info[armci_clus_me].nslave-1;

}
       

/*\ Substring Replacement: replace needle with nail in a haystack
\*/
static char *substr_replace(char *haystack, char *needle, char *nail)
{
char *tmp, *pos, *first;
size_t len=strlen(needle), nlen=strlen(nail),bytes;
ssize_t left;

    pos = strstr(haystack,needle);
    if (pos ==NULL) return NULL;
    first= tmp = calloc(strlen(haystack)+nlen-len+1+1,1);
    if(first==NULL) return(NULL);
    bytes = pos - haystack;
    while(bytes){ *tmp = *haystack; tmp++; haystack++; bytes--;}
    while(nlen) { *tmp = *nail; tmp++; nail++; nlen--;}
    haystack += len;
    left = strlen(haystack);
    while(left>0){*tmp = *haystack; tmp++; haystack++; left --;}
    *tmp='\0';
    return(first);
}


/*\ ARMCI_HOSTNAME_REPLACE contains "needle/nail" string to derive new hostname
\*/
static char *new_hostname(char *host)
{
  char *tmp, *needle, *nail;
  if((tmp =getenv("ARMCI_HOSTNAME_REPLACE"))){
      needle = strdup(tmp);
      if(needle== NULL) return NULL;
      nail = strchr(needle,'/');
      if(nail == NULL) return NULL;
      *nail = '\0';
      nail++;
      if(nail == (needle+1)){
        char* tmp1 = calloc(strlen(host)+strlen(nail)+1,1);
        if(tmp1 == NULL) return NULL;
        strcpy(tmp1,host);
        strcat(tmp1,nail);
        return tmp1;
      }
      return substr_replace(host,needle,nail);
  } else return NULL;
}


void armci_init_clusinfo()
{
  char name[MAX_HOSTNAME], *merged;
  int  i, len, limit, rc;
  char *tmp;

  if((tmp =getenv("ARMCI_HOSTNAME")) == NULL){
    limit = MAX_HOSTNAME-1;
    rc = GETHOSTNAME(name, limit);
    if(rc < 0)armci_die("armci: gethostname failed",rc);
    tmp = new_hostname(name);
  }
  if(tmp != NULL){
      if(strlen(tmp) >= MAX_HOSTNAME)
                        armci_die("armci: hostname too long",strlen(tmp));
      strcpy(name,tmp);
      printf("%d using %s hostname\n",armci_me, name);
      fflush(stdout);
  }
  
  len =  strlen(name);

#ifdef HOSTNAME_TRUNCATE
     /* in some cases (e.g.,SP) when name is used to determine
      * cluster structure but not to establish communication
      * we can truncate hostnames to save memory */
     limit = HOSTNAME_LEN-1;
     for(i=0; i<len; i++){
         if(name[i] =='.')break; /*we are not truncating 1st part of hostname*/
         if(i==limit)armci_die("Please increase HOSTNAME_LEN in ARMCI >",i+1);
     }
     if(len>limit)name[limit]='\0';
     len =limit;
#else
  if(len >= HOSTNAME_LEN-1)
     armci_die("armci: gethostname overrun name string length",len);
#endif

  if(DEBUG)
     fprintf(stderr,"%d: %s len=%d\n",armci_me, name,(int)strlen(name));


  /******************* development hack *********************/
#ifdef DEBUG_HACK
  name[len]='0'+armci_me/CLUSNODES; 
  name[len+1]='\0';
  len++;
#endif
  
#ifdef CLUSTER
  merged = merge_names(name); /* create hostname list */
  process_hostlist(merged);        /* compute cluster info */
  free(merged);
#else
  process_hostlist(name);        /* compute cluster info */
#endif

  armci_master = armci_clus_info[armci_clus_me].master;

  /******************* development hack *********************/
#ifdef DEBUG_HACK
  for(i=0;i<armci_nclus;i++){
     int len=strlen(armci_clus_info[i].hostname);
/*     fprintf(stderr,"----hostlen=%d\n",len);*/
     armci_clus_info[i].hostname[len-1]='\0';
  }
#endif

  if(PRINT_CLUSTER_INFO && armci_nclus >1 && armci_me ==0){
     printf("ARMCI configured for %d cluster nodes\n", armci_nclus);
     fflush(stdout);
  }

  if(armci_me==0 && DEBUG) for(i=0;i<armci_nclus;i++)
     printf("%s cluster:%d nodes:%d master=%d\n",armci_clus_info[i].hostname,i, 
                         armci_clus_info[i].nslave,armci_clus_info[i].master);

}


/*\ find cluster node id the specified process is on
\*/
int armci_clus_id(int p)
{
int from, to, found, c;

    if(p<0 || p >= armci_nproc)armci_die("armci_clus_id: out of range",p);

    if(p < armci_clus_first){ from = 0; to = armci_clus_me;}
    else {from = armci_clus_me; to = armci_nclus;}

    found = to-1;
    for(c = from; c< to-1; c++)
        if(p < armci_clus_info[c+1].master){
              found=c;
              break;
        }

    return (found);
}

