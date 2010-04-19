#ifndef __KernelFxLogger_h__
#define __KernelFxLogger_h__

static const char * FindShortPathName(const char *PN, unsigned int length)  __attribute__ ((unused)) ;
static const char * FindShortPathName(const char *PN, unsigned int length)
  {
  int slashcount = 0;
  int i;
  for( i = length-1; i >= 0 ; i-- )
    {
    if( PN[i] == '/' )
      {
      slashcount++;
      if( slashcount == 3 )
        break;
      }
    }
  return PN+i ;
  }


#define KernelFxLog(dbgcat, fmt, args...)     \
  do {                \
    if(dbgcat)        \
    {                 \
      static const char filename[] = __FILE__ ;  \
      printk(KERN_INFO " %5d %1X ..%20s %4d %30s() " fmt "\n",   \
          current->pid,     \
          current_thread_info()->cpu, \
          FindShortPathName(filename,sizeof(filename)), __LINE__, __FUNCTION__, ## args);    \
          }     \
  } while (0)


#endif
