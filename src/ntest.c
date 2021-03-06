#include <neuro/neuro.h>
#include <unistd.h>
#include <neuro/ns2net.h>
#include <string.h>
#include <search.h>

struct NSCounter { int success, timedOut, refused, unknownHost, bindSuccess, bindError; };

struct NSNetConnectionController *bindNSCC, *connectNSCC;

#define MAXCHAR 1024

char readBuffer[MAXCHAR];
int readLen;
static void clearReadBuffer(void)
{
  readLen = 0;
}

static void readBufferMustMatch(const char *str)
{
  rassert(readLen == strlen(str));
  rassert(strncmp(str, readBuffer, strlen(str)) == 0);
}


static void NSRHBytesRead(void *udata, const char *buf, int len)
{
    int i;
    for (i = 0; i < len; ++i) {
      rassert(readLen < MAXCHAR);
      readBuffer[readLen] = buf[i];
      readLen += 1;
      putchar(buf[i]);
    }
}

static void NSRHClosed(void *udata)
{
  printf("NSRH Stream closed.\n");
}

static void NSBHBindError(void *udata) {
  struct NSCounter *count = (struct NSCounter *) udata;
  count->bindError += 1;
}

static void NSCHUnknownHost(void *udata) {
  struct NSCounter *count = (struct NSCounter *) udata;
  count->unknownHost += 1;
}

static void NSCHTimedOut(void *udata) {
  struct NSCounter *count = (struct NSCounter *) udata;
  count->timedOut += 1;
}

static void NSCHRefused(void *udata) {
  struct NSCounter *count = (struct NSCounter *) udata;
  count->refused += 1;
}

static struct NSNetConnectionReadHandler nsrh;

static void NSBHBindSuccess(void *udata, struct NSNetConnectionController *nscc) {
  struct NSCounter *count = (struct NSCounter *) udata;
  int retval;
  count->bindSuccess += 1;
  retval = attachConnectionReadHandler(nscc, &nsrh, "bindsuccess");
  rassert(retval == 0);
  bindNSCC = nscc;
}

static void NSCHSuccess(void *udata, struct NSNetConnectionController *nscc) {
  int retval;
  struct NSCounter *count = (struct NSCounter *) udata;
  count->success += 1;
  retval = attachConnectionReadHandler(nscc, &nsrh, "connectsuccess");
  connectNSCC = nscc;
}

static void testNSConnect(void) {
  int portNum = 7211;
  struct NSCounter basic = { 0, 0, 0 };
  struct NSCounter cur;
  struct NSNetConnectionHandler nsch;
  struct NSNetBindHandler nsbh;
  struct NSNet *ns;

  nsrh.bytesRead = NSRHBytesRead;
  nsrh.closed = NSRHClosed;

  nsch.success = NSCHSuccess;
  nsch.timedOut = NSCHTimedOut;
  nsch.refused = NSCHRefused;
  nsch.unknownHost = NSCHUnknownHost;

  nsbh.success = NSBHBindSuccess;
  nsbh.error = NSBHBindError;

  cur = basic;
  ns = newNSNet();
  attemptConnect(ns, &nsch, "notAValidHostName", 5555, &cur);
  rassert(cur.success == 0 && cur.unknownHost == 1 && cur.refused == 0 && cur.timedOut == 0);
  cur = basic;
  rassert(0 == attemptConnect(ns, &nsch, "localhost", 5555, &cur));
  waitForNetEvent(ns, 2000);
  rassert(cur.success == 0 && cur.unknownHost == 0 && cur.refused == 1 && cur.timedOut == 0);
  cur = basic;
  rassert(0 == attemptBind(ns, &nsbh, 1, portNum, &cur));
  rassert(cur.bindSuccess == 0 && cur.bindError == 0);
  rassert(0 == attemptConnect(ns, &nsch, "localhost", portNum, &cur));
  waitForNetEvent(ns, 1000);
  rassert(cur.success == 1 && cur.unknownHost == 0 && cur.refused == 0 && cur.timedOut == 0);
  rassert(cur.bindSuccess == 1 && cur.bindError == 0);
  cur = basic;
  rassert(0 == attemptBind(ns, &nsbh, 0, portNum+1, &cur));
  rassert(cur.bindSuccess == 0 && cur.bindError == 0);
  rassert(0 == attemptConnect(ns, &nsch, "localhost", portNum+1, &cur));
  waitForNetEvent(ns, 1000);
  rassert(cur.bindSuccess == 1 && cur.bindError == 0);
  rassert(connectNSCC != NULL);
  rassert(bindNSCC != NULL);
  clearReadBuffer();
  { char *msg = "msgtoserver"; writeNSBytes(connectNSCC, msg, strlen(msg)); }
  waitForNetEvent(ns, 1000);
  readBufferMustMatch("msgtoserver");
  clearReadBuffer();
  { char *msg = "msgtoclient!"; 
    while (*msg) {
      writeNSBytes(bindNSCC, msg, 1);
      waitForNetEvent(ns, 1000);
      msg += 1;
    }
  }
  readBufferMustMatch("msgtoclient!");
}

static void testNSNet(void)
{
  testNSConnect();
  if (fork()) {
  }
  else {
  }
}

static int allSum = 0;
static void allStringTester
  (struct StringTable *st, const char *key, void *val, void *udata)
{
  allSum += key[0] - '0';
}

static void testStringTable(void)
{
  int val;
  struct StringTable *st;
  int i;
  for (i = 0; i < 100; i += 1) {
    st = newStringTable();
    rassert(st);
    rassert(findString(st, "dog") == NULL);
    rassert(putString(st, "cat", &val) == 0);
    rassert(findString(st, "cat") == &val);
    rassert(delString(st, "dog") == ERR_NOSTRING);
    rassert(delString(st, "cat") == 0);
    rassert(findString(st, "cat") == NULL);
    rassert(putString(st, "1", &val) == 0);
    rassert(putString(st, "2", &val) == 0);
    rassert(putString(st, "3", &val) == 0);
    allSum = 0;
    allStrings(st, allStringTester, NULL);
    rassert(allSum == 6);
    rassert(delString(st, "1") == 0);
    rassert(delString(st, "3") == 0);
    rassert(delString(st, "2") == 0);
    rassert(putString(st, "memcheck", &val) == 0);
    freeStringTable(st);
    }
}

static const char *gotParm;
static int gotInt;
static int gotUnknown;

static void handlePrint(struct CommandHandler *ch, int cliIndex) {
  gotParm = fetchStringParameter(ch, 0);
  fetchIntParameters(ch, &gotInt, 1);
}

static void handleUnknown(struct CommandHandler *ch, int cliIndex) {
  gotUnknown = 1;
}

static void testCommandHandler(void)
{
  struct CommandHandler *ch;
  gotUnknown = 0;
  gotParm = NULL;
  ch = newCommandHandler();
  newClientStarted(ch, 3);
  handleLine(ch, "what", 3);
  enregisterCommand(ch, "unknown", handleUnknown);
  enregisterCommand(ch, "print", handlePrint);
  rassert(gotUnknown == 0);
  handleLine(ch, "what", 3);
  rassert(gotUnknown == 1);
  handleLine(ch, "print 0", 3);
  rassert(gotParm != NULL);
  rassert(strcmp(gotParm, "0") == 0);
  rassert(gotInt == 0);
  handleLine(ch, "print \"\"", 3);
  rassert(strcmp(gotParm, "") == 0);
  rassert(gotInt == 0);
  handleLine(ch, "print \\\"", 3);
  rassert(strcmp(gotParm, "\"") == 0);
  rassert(gotInt == 0);
  handleLine(ch, "print 87", 3);
  rassert(gotInt == 87);
  rassert(strcmp(gotParm, "87") == 0);
  rassert(strcmp(gotParm, "87") == 0);
  handleLine(ch, "print \"45\"", 3);
  handleLine(ch, "print \"48\" ", 3);
  handleLine(ch, "print    \"14\" ", 3);
  handleLine(ch, "print   11  ", 3);
  handleLine(ch, "print \"hi\"", 3);
  handleLine(ch, "print \" hi\"", 3);
  handleLine(ch, "print \"h i\"", 3);
  handleLine(ch, "print \"hi \"", 3);
  handleLine(ch, "print \"h\\\\i\"", 3);
  handleLine(ch, "print \"h\\\"i\"", 3);
  freeCommandHandler(ch);
}

int main(int argc, char **argv)
{
  testStringTable();
  testCommandHandler();
  testNSNet();
  return 0;
}
