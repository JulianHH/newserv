#include <stdio.h>
#include <time.h>
#include <dirent.h>

#include "../nick/nick.h"
#include "../localuser/localuserchannel.h"
#include "../core/hooks.h"
#include "../core/schedule.h"
#include "../lib/array.h"
#include "../lib/base64.h"
#include "../lib/irc_string.h"
#include "../lib/splitline.h"

#include "qabot.h"

int qabot_dochananswer(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  int id;
  char* ch;
  qab_question* q;
  
  if (cargc < 2) {
    sendnoticetouser(bot->np, sender, "Syntax: !answer <id> <answer>");
    return CMD_ERROR;
  }
  
  id = strtol(cargv[0], NULL, 10);
  ch = cargv[1];
  
  if ((id < 1) || (id > bot->lastquestionID)) {
    sendnoticetouser(bot->np, sender, "Invalid question ID %d.", id);
    return CMD_ERROR;
  }
  
  for (q = bot->questions[id % QUESTIONHASHSIZE]; q; q = q->next)
    if (q->id == id)
      break;
  
  if (!q) {
    sendnoticetouser(bot->np, sender, "Can't find question %d.", id);
    return CMD_ERROR;
  }
  
  switch (q->flags & QAQ_QSTATE) {
  case QAQ_ANSWERED:
    sendnoticetouser(bot->np, sender, "Question %d has already been answered.", id);
    return CMD_ERROR;
    
  case QAQ_OFFTOPIC:
    sendnoticetouser(bot->np, sender, "Question %d has been marked as off-topic.", id);
    return CMD_ERROR;
  
  case QAQ_SPAM:
    sendnoticetouser(bot->np, sender, "Question %d has been marked as spam.", id);
    return CMD_ERROR;
  
  default:
    break;
  }
  
  q->flags = ((q->flags) & ~QAQ_QSTATE) | QAQ_ANSWERED;
  q->answer = strdup(ch);
  
  bot->answered++;
  
  if (!bot->nextspam && !bot->micnumeric) {
    sendmessagetochannel(bot->np, bot->public_chan->channel, "%s asked: %s", q->nick, q->question);
    sendmessagetochannel(bot->np, bot->public_chan->channel, "%s answers: %s", sender->nick, ch);
  }
  else {
    qab_answer* a;
    
    a = (qab_answer*)malloc(sizeof(qab_answer));
    a->question = q;
    strncpy(a->nick, sender->nick, NICKLEN);
    a->nick[NICKLEN] = '\0';
    a->next = bot->answers;
    bot->answers = a;
    
    sendnoticetouser(bot->np, sender, "Can't send your answer right now. Answer was stored and will be sent later on.");
    return CMD_OK;
  }
  
  sendnoticetouser(bot->np, sender, "Answer to question %d has been sent and stored.", id);
  
  return CMD_OK;
}

int qabot_dochanblock(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  qab_block* b;
  
  if (cargc < 1) {
    sendnoticetouser(bot->np, sender, "Syntax: !block [-q|-t] <account|mask>");
    return CMD_ERROR;
  }
  
  if (cargc > 1) {
    if (!ircd_strncmp(cargv[0], "-q", 2)) {
      /* account block */
      char* target = cargv[1];
      
      if (*target == '#') {
        target++;
        
        if (strchr(target, '*') || strchr(target, '?')) {
          sendnoticetouser(bot->np, sender, "Wildcard account blocks are not supported.");
          return CMD_ERROR;
        }
      }
      else {
        nick* tnick;
        
        if (!(tnick = getnickbynick(target))) {
          sendnoticetouser(bot->np, sender, "Couldn't find user %s.", target);
          return CMD_ERROR;
        }
        
        if (!IsAccount(tnick)) {
           sendnoticetouser(bot->np, sender, "%s is not authed.", tnick->nick);
           return CMD_ERROR;
        }
        
        target = tnick->authname;
      }
      
      b = (qab_block*)malloc(sizeof(qab_block));
      b->type = QABBLOCK_ACCOUNT;
      b->created = time(0);
      strncpy(b->creator, IsAccount(sender) ? sender->authname : "UNKNOWN", ACCOUNTLEN);
      b->creator[ACCOUNTLEN] = '\0';
      b->blockstr = strdup(target);
      b->prev = 0;
      b->next = bot->blocks;
      if (bot->blocks)
        bot->blocks->prev = b;
      bot->blocks = b;
      bot->block_count++;
      
      sendnoticetouser(bot->np, sender, "Now blocking all messages from users with accountname %s.", target);
      
      if (bot->flags & QAB_BLOCKMARK) {
        qab_question* q;
        int i, spamqcount = 0;
        nick* qnick;
        
        for (i = 0; i < QUESTIONHASHSIZE; i++) {
          for (q = bot->questions[i]; q; q = q->next) {
            if ((q->flags & QAQ_QSTATE) != QAQ_NEW)
              continue;
            
            if (!(qnick = getnickbynumeric(q->numeric)))
              continue;
            
            if (!IsAccount(qnick))
              continue;
            
            if (ircd_strcmp(qnick->authname, b->blockstr))
              continue;
            
            q->flags = ((q->flags) & ~QAQ_QSTATE) | QAQ_SPAM;
            spamqcount++;
          }
        }
        
        sendnoticetouser(bot->np, sender, "Block caused %d message%s to be marked as spam.", spamqcount, (spamqcount == 1) ? "" : "s");
      }
    }
    else if (!ircd_strncmp(cargv[0], "-t", 2)) {
      /* text block */
      char* mask = cargv[1];
      
      b = (qab_block*)malloc(sizeof(qab_block));
      b->type = QABBLOCK_TEXT;
      b->created = time(0);
      strncpy(b->creator, sender->authname, ACCOUNTLEN);
      b->creator[ACCOUNTLEN] = '\0';
      b->blockstr = strdup(mask);
      b->prev = 0;
      b->next = bot->blocks;
      if (bot->blocks)
        bot->blocks->prev = b;
      bot->blocks = b;
      bot->block_count++;
      
      sendnoticetouser(bot->np, sender, "Now blocking all questions which match %s.", mask);
      
      if (bot->flags & QAB_BLOCKMARK) {
        qab_question* q;
        int i, spamqcount = 0;
        
        for (i = 0; i < QUESTIONHASHSIZE; i++) {
          for (q = bot->questions[i]; q; q = q->next) {
            if ((q->flags & QAQ_QSTATE) != QAQ_NEW)
              continue;
            
            if (match(b->blockstr, q->question))
              continue;
            
            q->flags = ((q->flags) & ~QAQ_QSTATE) | QAQ_SPAM;
            spamqcount++;
          }
        }
        
        sendnoticetouser(bot->np, sender, "Block caused %d message%s to be marked as spam.", spamqcount, (spamqcount == 1) ? "" : "s");
      }
    }
    else {
      sendnoticetouser(bot->np, sender, "Invalid flag.");
      return CMD_ERROR;
    }
  }
  else {
    /* hostmask block */
    char* mask = cargv[0];
    
    if (!strchr(mask, '@') || !strchr(mask, '!')) {
      sendnoticetouser(bot->np, sender, "%s is not a valid hostmask.", mask);
      return CMD_ERROR;
    }
    
    b = (qab_block*)malloc(sizeof(qab_block));
    b->type = QABBLOCK_HOST;
    b->created = time(0);
    strncpy(b->creator, sender->authname, ACCOUNTLEN);
    b->creator[ACCOUNTLEN] = '\0';
    b->blockstr = strdup(mask);
    b->prev = 0;
    b->next = bot->blocks;
    if (bot->blocks)
      bot->blocks->prev = b;
    bot->blocks = b;
    bot->block_count++;
    
    sendnoticetouser(bot->np, sender, "Now blocking all messages from users with a hostmask matching %s.", mask);
    
    if (bot->flags & QAB_BLOCKMARK) {
        qab_question* q;
        int i, spamqcount = 0;
        nick* qnick;
        char hostbuf[NICKLEN + USERLEN + HOSTLEN + 3];
        
        for (i = 0; i < QUESTIONHASHSIZE; i++) {
          for (q = bot->questions[i]; q; q = q->next) {
            if ((q->flags & QAQ_QSTATE) != QAQ_NEW)
              continue;
            
            if (!(qnick = getnickbynumeric(q->numeric)))
              continue;
            
            sprintf(hostbuf,"%s!%s@%s", qnick->nick, qnick->ident, qnick->host->name->content);
            
            if (match(b->blockstr, hostbuf))
              continue;
            
            q->flags = ((q->flags) & ~QAQ_QSTATE) | QAQ_SPAM;
            spamqcount++;
          }
        }
        
        sendnoticetouser(bot->np, sender, "Block caused %d message%s to be marked as spam.", spamqcount, (spamqcount == 1) ? "" : "s");
      }
  }
  
  return CMD_OK;
}

int qabot_dochanclear(void* np, int cargc, char** cargv) {
  qab_bot* bot = qabot_getcurrentbot();
  channel* cp = qabot_getcurrentchannel();
  qab_spam* s;
  qab_spam* ns;
  
  for (s = bot->nextspam; s; s = ns) {
    ns = s->next;
    free(s->message);
    free(s);
  }
  
  bot->nextspam = bot->lastspam = 0;
  
  sendmessagetochannel(bot->np, cp, "Cleared message buffer.");
  if (bot->micnumeric) {
    bot->micnumeric = 0;
    sendmessagetochannel(bot->np, cp, "Mic deactivated.");
  }
  if (bot->recnumeric) {
    bot->recnumeric = 0;
    sendmessagetochannel(bot->np, cp, "Recorder deactivated.");
  }
  if (bot->recfile) {
    fclose(bot->recfile);
    bot->recfile = NULL;
  }
  if (bot->playfile) {
    fclose(bot->playfile);
    bot->playfile = NULL;
  }
  
  return CMD_OK;
}

int qabot_dochanclosechan(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  modechanges changes;
  
  localsetmodeinit(&changes, bot->public_chan->channel, bot->np);
  localdosetmode_simple(&changes, CHANMODE_INVITEONLY, 0);
  localsetmodeflush(&changes, 1);
  sendnoticetouser(bot->np, sender, "Public channel has been closed.");
  
  return CMD_OK;
}

int qabot_dochanconfig(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  char* opt;
  char* value;
  
  if (cargc < 1) {
    sendnoticetouser(bot->np, sender, "Syntax: !config <option> [<value>]");
    sendnoticetouser(bot->np, sender, "Displays or sets configuration option. Valid options are:");
    sendnoticetouser(bot->np, sender, "blockcontrol - Block questions containing control chars.");
    sendnoticetouser(bot->np, sender, "blockcolour  - Block questions containing colour.");
    sendnoticetouser(bot->np, sender, "blockmark    - Mark questions affected by blocks as spam.");
    sendnoticetouser(bot->np, sender, "authedonly   - Accept questions from authed users only.");
    sendnoticetouser(bot->np, sender, "linebreak    - Separate questions with line breaks.");
    sendnoticetouser(bot->np, sender, "flooddetect  - Attempt to detect floodclone spam.");
    sendnoticetouser(bot->np, sender, "floodblock   - Automatically block floodclone spam.");
    sendnoticetouser(bot->np, sender, "spamint      - Text spam interval.");
    sendnoticetouser(bot->np, sender, "nickblockint - Time to wait before sending another question.");
    sendnoticetouser(bot->np, sender, "queuedqint   - Queued answer spam interval.");
    sendnoticetouser(bot->np, sender, "mictimeout   - Idle time before mic is automatically disabled.");
    return CMD_ERROR;
  }
  
  opt = cargv[0];
  if (cargc == 2)
    value = cargv[1];
  else
    value = 0;
  
  if (!ircd_strcmp(opt, "blockcontrol")) {
    if (value) {
      if (!ircd_strcmp(value, "on")) {
        bot->flags |= QAB_CONTROLCHAR;
        sendnoticetouser(bot->np, sender, "Questions containing control characters will now be blocked.");
      }
      else if (!ircd_strcmp(value, "off")) {
        bot->flags &= (~QAB_CONTROLCHAR);
        sendnoticetouser(bot->np, sender, "Questions containing control characters will no longer be blocked.");
      }
      else
        sendnoticetouser(bot->np, sender, "Invalid option. Valid options are 'on' or 'off'.");
    }
    else
      sendnoticetouser(bot->np, sender, "Control characters are currently %s blocked.", (bot->flags & QAB_CONTROLCHAR) ? "being" : "not being");
  }
  else if (!ircd_strcmp(opt, "blockcolour")) {
    if (value) {
      if (!ircd_strcmp(value, "on")) {
        bot->flags |= QAB_COLOUR;
        sendnoticetouser(bot->np, sender, "Questions containing colour will now be blocked.");
      }
      else if (!ircd_strcmp(value, "off")) {
        bot->flags &= (~QAB_COLOUR);
        sendnoticetouser(bot->np, sender, "Questions containing colour will no longer be blocked.");
      }
      else
        sendnoticetouser(bot->np, sender, "Invalid option. Valid options are 'on' or 'off'.");
    }
    else
      sendnoticetouser(bot->np, sender, "Colours are currently %s blocked.", (bot->flags & QAB_COLOUR) ? "being" : "not being");
  }
  else if (!ircd_strcmp(opt, "blockmark")) {
    if (value) {
      if (!ircd_strcmp(value, "on")) {
        bot->flags |= QAB_BLOCKMARK;
        sendnoticetouser(bot->np, sender, "New blocks will automatically mark affected questions as spam.");
      }
      else if (!ircd_strcmp(value, "off")) {
        bot->flags &= (~QAB_BLOCKMARK);
        sendnoticetouser(bot->np, sender, "New blocks will no longer automatically mark affected questions as spam.");
      }
      else
        sendnoticetouser(bot->np, sender, "Invalid option. Valid options are 'on' or 'off'.");
    }
    else
      sendnoticetouser(bot->np, sender, "Blocks are currently %smarking affected questions as spam.", (bot->flags & QAB_BLOCKMARK) ? "" : "not ");
  }
  else if (!ircd_strcmp(opt, "authedonly")) {
    if (value) {
      if (!ircd_strcmp(value, "on")) {
        bot->flags |= QAB_AUTHEDONLY;
        sendnoticetouser(bot->np, sender, "Questions from unauthed users will now be blocked.");
      }
      else if (!ircd_strcmp(value, "off")) {
        bot->flags &= (~QAB_AUTHEDONLY);
        sendnoticetouser(bot->np, sender, "Questions from unauthed users will no longer be blocked.");
      }
      else
        sendnoticetouser(bot->np, sender, "Invalid option. Valid options are 'on' or 'off'.");
    }
    else
      sendnoticetouser(bot->np, sender, "Unauthed users may currently %ssend questions.", (bot->flags & QAB_AUTHEDONLY) ? "NOT " : "");
  }
  else if (!ircd_strcmp(opt, "linebreak")) {
    if (value) {
      if (!ircd_strcmp(value, "on")) {
        bot->flags |= QAB_LINEBREAK;
        sendnoticetouser(bot->np, sender, "Line breaks are now enabled.");
      }
      else if (!ircd_strcmp(value, "off")) {
        bot->flags &= (~QAB_LINEBREAK);
        sendnoticetouser(bot->np, sender, "Line breaks are now disabled.");
      }
      else
        sendnoticetouser(bot->np, sender, "Invalid option. Valid options are 'on' or 'off'.");
    }
    else
      sendnoticetouser(bot->np, sender, "Line breaks are currently %s.", (bot->flags & QAB_LINEBREAK) ? "enabled" : "disabled");
  }
  else if (!ircd_strcmp(opt, "flooddetect")) {
    if (value) {
      if (!ircd_strcmp(value, "on")) {
        bot->flags |= QAB_FLOODDETECT;
        sendnoticetouser(bot->np, sender, "Flood detection is now enabled.");
      }
      else if (!ircd_strcmp(value, "off")) {
        bot->flags &= (~QAB_FLOODDETECT);
        sendnoticetouser(bot->np, sender, "Flood detection is now disabled.");
      }
      else
        sendnoticetouser(bot->np, sender, "Invalid option. Valid options are 'on' or 'off'.");
    }
    else
      sendnoticetouser(bot->np, sender, "Flood detection is currently %s.", (bot->flags & QAB_FLOODDETECT) ? "enabled" : "disabled");
  }
  else if (!ircd_strcmp(opt, "floodblock")) {
    if (value) {
      if (!ircd_strcmp(value, "on")) {
        bot->flags |= QAB_FLOODSTOP;
        sendnoticetouser(bot->np, sender, "Flood blocking is now enabled.");
      }
      else if (!ircd_strcmp(value, "off")) {
        bot->flags &= (~QAB_FLOODSTOP);
        sendnoticetouser(bot->np, sender, "Flood blocking is now disabled.");
      }
      else
        sendnoticetouser(bot->np, sender, "Invalid option. Valid options are 'on' or 'off'.");
    }
    else
      sendnoticetouser(bot->np, sender, "Flood blocking is currently %s.", (bot->flags & QAB_FLOODSTOP) ? "enabled" : "disabled");
  }
  else if (!ircd_strcmp(opt, "mictimeout")) {
    if (value) {
      int v = (int)strtol(value, NULL, 10);
      
      if ((v < 0) || (v > 300)) {
        sendnoticetouser(bot->np, sender, "Value must be between 0 (off) and 300.");
        return CMD_ERROR;
      }
      
      bot->mic_timeout = v;
      sendnoticetouser(bot->np, sender, "Value set.");
    }
    else {
      if (bot->mic_timeout)
        sendnoticetouser(bot->np, sender, "Mic timeout is currently %d second%s.", bot->mic_timeout, (bot->mic_timeout == 1) ? "" : "s");
      else
        sendnoticetouser(bot->np, sender, "Mic timeout is currently disabled.");
    }
  }
  else if (!ircd_strcmp(opt, "spamint")) {
    if (value) {
      int v = (int)strtol(value, NULL, 10);
      
      if ((v < 1) || (v > 30)) {
        sendnoticetouser(bot->np, sender, "Value must be between 1 and 30.");
        return CMD_ERROR;
      }
      
      bot->spam_interval = v;
      sendnoticetouser(bot->np, sender, "Value set.");
    }
    else
      sendnoticetouser(bot->np, sender, "Spam interval is currently %d second%s.", bot->spam_interval, (bot->spam_interval == 1) ? "" : "s");
  }
  else if (!ircd_strcmp(opt, "nickblockint")) {
    if (value) {
      int v = (int)strtol(value, NULL, 10);
      
      if ((v < 1) || (v > 300)) {
        sendnoticetouser(bot->np, sender, "Value must be between 1 and 300.");
        return CMD_ERROR;
      }
      
      bot->ask_wait = v;
      sendnoticetouser(bot->np, sender, "Value set.");
    }
    else
      sendnoticetouser(bot->np, sender, "Nick block interval is currently %d second%s.", bot->ask_wait, (bot->ask_wait == 1) ? "" : "s");
  }
  else if (!ircd_strcmp(opt, "queuedqint")) {
    if (value) {
      int v = (int)strtol(value, NULL, 10);
      
      if ((v < 1) || (v > 20)) {
        sendnoticetouser(bot->np, sender, "Value must be between 1 and 20.");
        return CMD_ERROR;
      }
      
      bot->queued_question_interval = v;
      sendnoticetouser(bot->np, sender, "Value set.");
    }
    else
      sendnoticetouser(bot->np, sender, "Queued question interval is currently %d second%s.", bot->queued_question_interval, (bot->queued_question_interval == 1) ? "" : "s");
  }
  else
    sendnoticetouser(bot->np, sender, "Invalid configuration option.");
  
  return CMD_OK;
}

int qabot_dochanhelp(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  char* ch;
  
  if (cargc < 1)
    ch = "";
  else
    ch = cargv[0];
  
  if (*ch) {
    if (!ircd_strcmp(ch, "record")) {
      sendnoticetouser(bot->np, sender, "Syntax: !record [filename]");
      sendnoticetouser(bot->np, sender, "Turn the recorder on or off. When turned on, anything said is recorded.");
      sendnoticetouser(bot->np, sender, "File name is required when starting the recorder.");
    }
    else if (!ircd_strcmp(ch, "play")) {
      sendnoticetouser(bot->np, sender, "Syntax: !play <filename>");
      sendnoticetouser(bot->np, sender, "Begin playback from the file specified.");
    }
    else if (!ircd_strcmp(ch, "continue")) {
      sendnoticetouser(bot->np, sender, "Syntax: !continue");
      sendnoticetouser(bot->np, sender, "Continue playback from a file.");
    }
    else if (!ircd_strcmp(ch, "stop")) {
      sendnoticetouser(bot->np, sender, "Syntax: !stop");
      sendnoticetouser(bot->np, sender, "Stop playback of a file before it reaches the end.");
    }
    else if (!ircd_strcmp(ch, "list")) {
      sendnoticetouser(bot->np, sender, "Syntax: !list");
      sendnoticetouser(bot->np, sender, "Lists recording files.");
    }
    else if (!ircd_strcmp(ch, "delete")) {
      sendnoticetouser(bot->np, sender, "Syntax: !delete <filename>");
      sendnoticetouser(bot->np, sender, "Deletes a recording file.");
    }
    else if (!ircd_strcmp(ch, "mic")) {
      sendnoticetouser(bot->np, sender, "Syntax: !mic");
      sendnoticetouser(bot->np, sender, "Turn the microphone on or off. When turned on, anything said by the microphone holder is relayed to %s.", bot->public_chan->name->content);
    }
    else if (!ircd_strcmp(ch, "clear")) {
      sendnoticetouser(bot->np, sender, "Syntax: !clear");
      sendnoticetouser(bot->np, sender, "Clear currently queued text to relay, and turn off the microphone.");
    }
    else if (!ircd_strcmp(ch, "ping")) {
      sendnoticetouser(bot->np, sender, "Syntax: !ping");
      sendnoticetouser(bot->np, sender, "Pings the bot.");
    }
    else if (!ircd_strcmp(ch, "config")) {
      sendnoticetouser(bot->np, sender, "Syntax: !config <option|help> [<value>]");
      sendnoticetouser(bot->np, sender, "Display or set bot configuration options.");
    }
    else if (!ircd_strcmp(ch, "answer")) {
      sendnoticetouser(bot->np, sender, "Syntax: !answer <id> <answer>");
      sendnoticetouser(bot->np, sender, "Answer a question.");
    }
    else if (!ircd_strcmp(ch, "block")) {
      sendnoticetouser(bot->np, sender, "Syntax: !block [<-q|-t>] <mask>");
      sendnoticetouser(bot->np, sender, "Add a block, where:");
      sendnoticetouser(bot->np, sender, "-q: blocks a Q account.");
      sendnoticetouser(bot->np, sender, "-t: blocks question text.");
      sendnoticetouser(bot->np, sender, "No flag results in a hostmask block.");
    }
    else if (!ircd_strcmp(ch, "listblocks")) {
      sendnoticetouser(bot->np, sender, "Syntax: !listblocks");
      sendnoticetouser(bot->np, sender, "View the currently added blocks.");
    }
    else if (!ircd_strcmp(ch, "spam")) {
      sendnoticetouser(bot->np, sender, "Syntax: !spam <id>");
      sendnoticetouser(bot->np, sender, "Mark a question as spam. This stops it being answered.");
    }
    else if (!ircd_strcmp(ch, "offtopic")) {
      sendnoticetouser(bot->np, sender, "Syntax: !offtopic <id>");
      sendnoticetouser(bot->np, sender, "Mark a question as off-topic. This stops it being answered.");
    }
    else if (!ircd_strcmp(ch, "unblock")) {
      sendnoticetouser(bot->np, sender, "Syntax: !unblock [<-q|-t>] <mask>");
      sendnoticetouser(bot->np, sender, "Removes a block. See \"!help block\" for a description of the flags.");
    }
    else if (!ircd_strcmp(ch, "reset")) {
      sendnoticetouser(bot->np, sender, "Syntax: !reset <all|questions|blocks|stats>");
      sendnoticetouser(bot->np, sender, "Reset the questions, blocks or both; or the stats.");
    }
    else if (!ircd_strcmp(ch, "closechan")) {
      sendnoticetouser(bot->np, sender, "Syntax: !closechan");
      sendnoticetouser(bot->np, sender, "Closes the public channel.");
    }
    else if (!ircd_strcmp(ch, "openchan")) {
      sendnoticetouser(bot->np, sender, "Syntax: !openchan");
      sendnoticetouser(bot->np, sender, "Opens the public channel.");
    }
    else if (!ircd_strcmp(ch, "status")) {
      sendnoticetouser(bot->np, sender, "Syntax: !status");
      sendnoticetouser(bot->np, sender, "Displays some status information and statistics.");
    }
    else if (!ircd_strcmp(ch, "help")) {
      sendnoticetouser(bot->np, sender, "Syntax !help [<command>]");
      sendnoticetouser(bot->np, sender, "List available commands or view help for a particular command.");
    }
    else {
      sendnoticetouser(bot->np, sender, "No help available for '%s'.", ch);
    }
  }
  else {
    sendnoticetouser(bot->np, sender, "The following channel commands are recognised:");
    sendnoticetouser(bot->np, sender, "!answer     - Answer a question.");
    sendnoticetouser(bot->np, sender, "!block      - Block a hostmask, account or string.");
    sendnoticetouser(bot->np, sender, "!clear      - Clear currently queued text to spam.");
    sendnoticetouser(bot->np, sender, "!closechan  - Close the public channel.");
    sendnoticetouser(bot->np, sender, "!config     - Display or set bot configuration options.");
    sendnoticetouser(bot->np, sender, "!help       - List commands or view the help for a command");
    sendnoticetouser(bot->np, sender, "!listblocks - List currently added blocks.");
    sendnoticetouser(bot->np, sender, "!mic        - Turn the microphone on or off.");
    sendnoticetouser(bot->np, sender, "!offtopic   - Mark a question or questions as off-topic.");
    sendnoticetouser(bot->np, sender, "!openchan   - Open the public channel.");
    sendnoticetouser(bot->np, sender, "!ping       - Ping the bot.");
    sendnoticetouser(bot->np, sender, "!reset      - Clear all blocks, questions or both.");
    sendnoticetouser(bot->np, sender, "!spam       - Mark a question or questions as spam.");
    sendnoticetouser(bot->np, sender, "!status     - Display some status statistics.");
    sendnoticetouser(bot->np, sender, "!unblock    - Remove a block.");
    sendnoticetouser(bot->np, sender, "!record     - Turn the recorder on or off.");
    sendnoticetouser(bot->np, sender, "!play       - Start playback of a recording.");
    sendnoticetouser(bot->np, sender, "!continue   - Continue playback of a recording.");
    sendnoticetouser(bot->np, sender, "!stop       - Stop playback of a recording.");
    sendnoticetouser(bot->np, sender, "!list       - Lists recordings.");
    sendnoticetouser(bot->np, sender, "!delete     - Deletes a previous recording.");
    sendnoticetouser(bot->np, sender, "End of list.");
  }
  
  return CMD_OK;
}

int qabot_dochanlistblocks(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  qab_block* b;
  
  if (!(b = bot->blocks)) {
    sendnoticetouser(bot->np, sender, "There are no blocks currently added.");
    return CMD_ERROR;
  }
  
  sendnoticetouser(bot->np, sender, "Type: Hostmask/Account/Textmask:");
  
  for (; b; b = b->next) {
    if (b->type == QABBLOCK_ACCOUNT)
      sendnoticetouser(bot->np, sender, "A     %s", b->blockstr);
    else if (b->type == QABBLOCK_HOST)
      sendnoticetouser(bot->np, sender, "H     %s", b->blockstr);
    else
      sendnoticetouser(bot->np, sender, "T     %s", b->blockstr);
  }
  
  sendnoticetouser(bot->np, sender, "End of list.");
  
  return CMD_OK;
}

int qabot_dochanmic(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  channel* cp = qabot_getcurrentchannel();
  
  if (bot->micnumeric) {
    if (bot->micnumeric == sender->numeric) {
      bot->micnumeric = 0;
      sendmessagetochannel(bot->np, cp, "Mic deactivated.");
      if (!bot->lastspam)
        qabot_spamstored((void*)bot);
    }
    else {
      bot->lastmic = time(NULL);
      bot->micnumeric = sender->numeric;
      sendmessagetochannel(bot->np, cp, "%s now has the mic. Anything said by %s will be relayed in %s.", 
        sender->nick, sender->nick, bot->public_chan->name->content);
      deleteschedule(0, qabot_spamstored, (void*)bot);
    }
  }
  else {
    bot->lastmic = time(NULL);
    bot->micnumeric = sender->numeric;
    sendmessagetochannel(bot->np, cp, "Mic activated. Anything said by %s will be relayed in %s.", 
      sender->nick, bot->public_chan->name->content);
    deleteschedule(0, qabot_spamstored, (void*)bot);
  }
  
  return CMD_OK;
}

int qabot_dochanrecord(void *np, int cargc, char** cargv) {
  char buf[200];
  unsigned int i, filenamelen;
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  channel* cp = qabot_getcurrentchannel();
  
  if (bot->recnumeric) {
    if (bot->recnumeric == sender->numeric) {
      bot->recnumeric = 0;
      fclose(bot->recfile);
      bot->recfile = NULL;
      sendmessagetochannel(bot->np, cp, "Recorder deactivated.");
    } else {
      sendmessagetochannel(bot->np, cp, "Someone else is recording at the moment.");
      return CMD_ERROR;
    }
  }
  else {
    if (bot->playfile) {
      sendmessagetochannel(bot->np, cp, "You cannot record whilst a playback is in progress.");
      return CMD_ERROR;
    }
    
    if (cargc < 1) {
      sendmessagetochannel(bot->np, cp, "You did not specify a file name.");
      return CMD_ERROR;
    }
    
    filenamelen = strlen(cargv[0]);
    
    if (filenamelen > 50) {
      sendmessagetochannel(bot->np, cp, "File name too long.");
      return CMD_ERROR;
    }
    
    for (i = 0; i < filenamelen; i++) {
      if (cargv[0][i] < '0' || (cargv[0][i] > '9' && cargv[0][i] < 'A') || (cargv[0][i] > 'Z' && cargv[0][i] < 'a') || cargv[0][i] > 'z') {
        sendmessagetochannel(bot->np, cp, "Invalid characters in file name.");
        return CMD_ERROR;
      }
    }
    
    snprintf(buf, 150, "./qabotrecords/%s_%s", bot->nick, cargv[0]);
    bot->recfile = fopen(buf, "w");
    
    if (!(bot->recfile)) {
      sendmessagetochannel(bot->np, cp, "Could not open record file.");
      return CMD_ERROR;
    }
    
    bot->recnumeric = sender->numeric;
    
    sendmessagetochannel(bot->np, cp, "Recorder activated. Anything said by %s will be recorded.", 
      sender->nick);
  }
  return CMD_OK;
}

int qabot_dochanplay(void *np, int cargc, char** cargv) {
  char buf[200];
  unsigned int i, filenamelen;
  qab_bot* bot = qabot_getcurrentbot();
  channel* cp = qabot_getcurrentchannel();
  
  if (bot->playfile) {
    sendmessagetochannel(bot->np, cp, "A playback is already in progress, use !stop to abort current playback.");
    return CMD_ERROR;
  }
  
  if (bot->recfile) {
    sendmessagetochannel(bot->np, cp, "You cannot playback whilst recording is in progress.");
    return CMD_ERROR;
  }
  
  if (cargc < 1) {
    sendmessagetochannel(bot->np, cp, "You did not specify a file name.");
    return CMD_ERROR;
  }
  
  filenamelen = strlen(cargv[0]);
  
  if (filenamelen > 50) {
    sendmessagetochannel(bot->np, cp, "File name too long.");
    return CMD_ERROR;
  }
  
  for (i = 0; i < filenamelen; i++) {
    if (cargv[0][i] < '0' || (cargv[0][i] > '9' && cargv[0][i] < 'A') || (cargv[0][i] > 'Z' && cargv[0][i] < 'a') || cargv[0][i] > 'z') {
      sendmessagetochannel(bot->np, cp, "Invalid characters in file name.");
      return CMD_ERROR;
    }
  }
  
  snprintf(buf, 150, "./qabotrecords/%s_%s", bot->nick, cargv[0]);
  bot->playfile = fopen(buf, "r");
  
  if (!(bot->playfile)) {
    sendmessagetochannel(bot->np, cp, "Could not open playback file.");
    return CMD_ERROR;
  }
  
  sendmessagetochannel(bot->np, cp, "Starting playback...");
  qabot_playback(bot);
  return CMD_OK;
}

int qabot_dochancontinue(void *np, int cargc, char** cargv) {
  qab_bot* bot = qabot_getcurrentbot();
  channel* cp = qabot_getcurrentchannel();
  
  if (!(bot->playfile)) {
    sendmessagetochannel(bot->np, cp, "No playback in progress.");
    return CMD_ERROR;
  }
  
  sendmessagetochannel(bot->np, cp, "Continuing playback...");
  qabot_playback(bot);
  return CMD_OK;
}

int qabot_dochanstop(void *np, int cargc, char** cargv) {
  qab_bot* bot = qabot_getcurrentbot();
  channel* cp = qabot_getcurrentchannel();
  
  if (!(bot->playfile)) {
    sendmessagetochannel(bot->np, cp, "No playback in progress.");
    return CMD_ERROR;
  }
  
  fclose(bot->playfile);
  bot->playfile = NULL;
  sendmessagetochannel(bot->np, cp, "Stopped playback.");
  return CMD_OK;
}

int qabot_dochanlist(void *np, int cargc, char** cargv) {
  DIR *recordlist;
  struct dirent *direntry;
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  
  recordlist = opendir("./qabotrecords");
  
  if (!recordlist) {
    sendnoticetouser(bot->np, sender, "Unable to retreive directory list.");
    return CMD_ERROR;
  }
  
  sendnoticetouser(bot->np, sender, "Recording list:");
  
  for (direntry = readdir(recordlist); direntry; direntry = readdir(recordlist)) {
    if (direntry->d_name[0] == '.')
      continue;
    
    sendnoticetouser(bot->np, sender, "  %s", direntry->d_name);
  }
  
  sendnoticetouser(bot->np, sender, "End of list.");
  closedir(recordlist);
  
  return CMD_OK;
}

int qabot_dochandelete(void *np, int cargc, char** cargv) {
  char buf[200];
  unsigned int i, filenamelen;
  qab_bot* bot = qabot_getcurrentbot();
  channel* cp = qabot_getcurrentchannel();
  
  if (bot->playfile) {
    sendmessagetochannel(bot->np, cp, "You cannot delete recordings whilst playback is in progress.");
    return CMD_ERROR;
  }
  
  if (bot->recfile) {
    sendmessagetochannel(bot->np, cp, "You cannot delete recordings whilst recording is in progress.");
    return CMD_ERROR;
  }
  
  if (cargc < 1) {
    sendmessagetochannel(bot->np, cp, "You did not specify a file name.");
    return CMD_ERROR;
  }
  
  filenamelen = strlen(cargv[0]);
  
  if (filenamelen > 50) {
    sendmessagetochannel(bot->np, cp, "File name too long.");
    return CMD_ERROR;
  }
  
  for (i = 0; i < filenamelen; i++) {
    if (cargv[0][i] < '0' || (cargv[0][i] > '9' && cargv[0][i] < 'A') || (cargv[0][i] > 'Z' && cargv[0][i] < 'a') || cargv[0][i] > 'z') {
      sendmessagetochannel(bot->np, cp, "Invalid characters in file name.");
      return CMD_ERROR;
    }
  }
  
  snprintf(buf, 150, "./qabotrecords/%s_%s", bot->nick, cargv[0]);
  
  if (!remove(buf)) {
    sendmessagetochannel(bot->np, cp, "Recording deleted.");
  } else {
    sendmessagetochannel(bot->np, cp, "Unable to delete recording.");
  }
  
  return CMD_OK;
}

int qabot_dochanmoo(void* np, int cargc, char** cargv) {
  qab_bot* bot = qabot_getcurrentbot();
  char moostr[50];
  int i, moocount = 5 + (rand() % 40);
  channel* cp = qabot_getcurrentchannel();
  
  moostr[0] = 'm';
  for (i = 1; i < moocount; i++)
    moostr[i] = ((rand() % 100) > 50) ? 'o': '0';
  moostr[i] = '\0';
  
  sendmessagetochannel(bot->np, cp, "%s", moostr);
  
  return CMD_OK;
}

int qabot_dochanofftopic(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  int id;
  int i;
  qab_question* q;
  
  if (cargc < 1) {
    sendnoticetouser(bot->np, sender, "Syntax: !spam <id> [<id> ... <id>]");
    return CMD_ERROR;
  }
  
  for (i = 0; i < cargc; i++) {
    id = strtol(cargv[i], NULL, 10);
    
    if ((id < 1) || (id > bot->lastquestionID)) {
      sendnoticetouser(bot->np, sender, "Invalid question ID %d.", id);
      continue;
    }
    
    for (q = bot->questions[id % QUESTIONHASHSIZE]; q; q = q->next)
      if (q->id == id)
        break;         
    
    if (!q) {
      sendnoticetouser(bot->np, sender, "Can't find question %d.", id);
      continue;
    }
    
    switch (q->flags & QAQ_QSTATE) {
    case QAQ_ANSWERED:
      sendnoticetouser(bot->np, sender, "Question %d has already been answered.", id);
      continue;
    
    case QAQ_OFFTOPIC:
      sendnoticetouser(bot->np, sender, "Question %d has already been marked as off-topic.", id);
      continue;
    
    case QAQ_SPAM:
      sendnoticetouser(bot->np, sender, "Question %d has already been marked as spam.", id);
      continue;
    
    default:
      break;
    }
    
    q->flags = ((q->flags) & ~QAQ_QSTATE) | QAQ_OFFTOPIC;
    sendnoticetouser(bot->np, sender, "Question %d has been marked as off-topic.", id);
  }
  
  return CMD_OK;
}

int qabot_dochanopenchan(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  modechanges changes;
  
  localsetmodeinit(&changes, bot->public_chan->channel, bot->np);
  localdosetmode_simple(&changes, CHANMODE_MODERATE|CHANMODE_DELJOINS, CHANMODE_INVITEONLY);
  localsetmodeflush(&changes, 1);
  sendnoticetouser(bot->np, sender, "Public channel has been opened.");
  
  return CMD_OK;
}

int qabot_dochanping(void* np, int cargc, char** cargv) {
  qab_bot* bot = qabot_getcurrentbot();
  channel* cp = qabot_getcurrentchannel();
  
  sendmessagetochannel(bot->np, cp, "pong!");
  
  return CMD_OK;
}

int qabot_dochanreset(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  int r = 0;
  
  if (cargc < 1) {
    sendnoticetouser(bot->np, sender, "Syntax: !reset <blocks|questions|stats|all>");
    return CMD_ERROR;
  }
  
  if (!ircd_strcmp(cargv[0], "blocks"))
    r = 1;
  else if (!ircd_strcmp(cargv[0], "questions"))
    r = 2;
  else if (!ircd_strcmp(cargv[0], "stats"))
    r = 4;
  else if (!ircd_strcmp(cargv[0], "all"))
    r = 3;
  else {
    sendnoticetouser(bot->np, sender, "Unknown parameter: %s.", cargv[0]);
    return CMD_ERROR;
  }
  
  if (r & 1) {
    qab_block* b;
   
    while (bot->blocks) {
      b = bot->blocks;
      bot->blocks = bot->blocks->next;
      if (b->blockstr)
        free(b->blockstr);
      free(b);
    }
    
    bot->block_count = 0;
    
    sendnoticetouser(bot->np, sender, "Reset (blocks): Done.");
  }
  
  if (r & 2) {
    qab_question* q;
    int i;
    
    for (i = 0; i < QUESTIONHASHSIZE; i++) {
      while (bot->questions[i]) {
        q = bot->questions[i];
        bot->questions[i] = bot->questions[i]->next;
        if (q->question)
          free(q->question);
        if (q->answer)
          free(q->answer);
        free(q);
      }
    }
    
    bot->lastquestionID = 0;
    bot->answered = 0;
    
    sendnoticetouser(bot->np, sender, "Reset (questions): Done.");
  }
  
  if (r & 4) {
    bot->answered = 0;
    bot->spammed = 0;
    sendnoticetouser(bot->np, sender, "Reset (stats): Done.");
  }
  
  return CMD_OK;
}

int qabot_dochanspam(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  int id;
  int i;
  qab_question* q;
  
  if (cargc < 1) {
    sendnoticetouser(bot->np, sender, "Syntax: !spam <id> [<id> ... <id>]");
    return CMD_ERROR;
  }
  
  for (i = 0; i < cargc; i++) {
    id = strtol(cargv[i], NULL, 10);
    
    if ((id < 1) || (id > bot->lastquestionID)) {
      sendnoticetouser(bot->np, sender, "Invalid question ID %d.", id);
      continue;
    }
    
    for (q = bot->questions[id % QUESTIONHASHSIZE]; q; q = q->next)
      if (q->id == id)
        break;         
    
    if (!q) {
      sendnoticetouser(bot->np, sender, "Can't find question %d.", id);
      continue;
    }
    
    switch (q->flags & QAQ_QSTATE) {
    case QAQ_ANSWERED:
      sendnoticetouser(bot->np, sender, "Question %d has already been answered.", id);
      continue;
    
    case QAQ_OFFTOPIC:
      sendnoticetouser(bot->np, sender, "Question %d has already been marked as off-topic.", id);
      continue;
    
    case QAQ_SPAM:
      sendnoticetouser(bot->np, sender, "Question %d has already been marked as spam.", id);
      continue;
    
    default:
      break;
    }
    
    q->flags = ((q->flags) & ~QAQ_QSTATE) | QAQ_SPAM;
    sendnoticetouser(bot->np, sender, "Question %d has been marked as spam.", id);
  }
  
  return CMD_OK;
}

int qabot_dochanstatus(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  
  sendnoticetouser(bot->np, sender, "Lines spammed:            %d", bot->spammed);
  sendnoticetouser(bot->np, sender, "Questions asked:          %d", bot->lastquestionID);
  sendnoticetouser(bot->np, sender, "Questions answered:       %d", bot->answered);
  sendnoticetouser(bot->np, sender, "Blocks:                   %d", bot->block_count);
  /*sendnoticetouser(bot->np, sender, "Question interval:        %d seconds", bot->question_interval);*/
  sendnoticetouser(bot->np, sender, "Spam interval:            %d seconds", bot->spam_interval);
  sendnoticetouser(bot->np, sender, "Nick block interval:      %d seconds", bot->ask_wait);
  sendnoticetouser(bot->np, sender, "Queued question interval: %d seconds", bot->queued_question_interval);
  sendnoticetouser(bot->np, sender, "Block control chars:      %s", (bot->flags & QAB_CONTROLCHAR) ? "Yes" : "No");
  sendnoticetouser(bot->np, sender, "Block colour:             %s", (bot->flags & QAB_COLOUR) ? "Yes" : "No");
  sendnoticetouser(bot->np, sender, "Authed users only:        %s", (bot->flags & QAB_AUTHEDONLY) ? "Yes" : "No");
  sendnoticetouser(bot->np, sender, "Line break:               %s", (bot->flags & QAB_LINEBREAK) ? "Yes" : "No");
  sendnoticetouser(bot->np, sender, "Question flood detection: %s", (bot->flags & QAB_FLOODDETECT) ? "Yes" : "No");
  sendnoticetouser(bot->np, sender, "Question flood blocking:  %s", (bot->flags & QAB_FLOODSTOP) ? "Yes" : "No");
  sendnoticetouser(bot->np, sender, "Blocks mark as spam:      %s", (bot->flags & QAB_BLOCKMARK) ? "Yes" : "No");
  if (bot->micnumeric) {
    nick* mnick = getnickbynumeric(bot->micnumeric);
    
    sendnoticetouser(bot->np, np, "Mic:                      Enabled (%s)", mnick ? mnick->nick : "UNKNOWN");
  }
  else
    sendnoticetouser(bot->np, np, "Mic:                      Disabled");
  sendnoticetouser(bot->np, sender, "Mic timeout:              %d", bot->mic_timeout);
  /*sendnoticetouser(bot->np, sender, "");*/
  
  return CMD_OK;
}

int qabot_dochanunblock(void* np, int cargc, char** cargv) {
  nick* sender = (nick*)np;
  qab_bot* bot = qabot_getcurrentbot();
  char* ch;
  qab_block* b;
  char type = -1;
  
  if (cargc < 1) {
    sendnoticetouser(bot->np, sender, "Syntax: !unblock [-q|-t] <account|mask>");
    return CMD_ERROR;
  }
  
  if (cargc > 1) {
    if (!ircd_strncmp(cargv[0], "-q", 2)) {
      type = QABBLOCK_ACCOUNT;
      ch = cargv[1];
      
      if (*ch == '#')
        ch++;
    }
    else if (!ircd_strncmp(cargv[0], "-t", 2)) {
      type = QABBLOCK_TEXT;
      ch = cargv[1];
    }
    else {
      sendnoticetouser(bot->np, sender, "Invalid flag.");
      return CMD_ERROR;
    }
  }
  else {
    type = QABBLOCK_HOST;
    ch = cargv[0];
  }
  
  for (b = bot->blocks; b; b = b->next) {
    if (b->type != type)
      continue;
    
    if (!ircd_strcmp(b->blockstr, ch)) {
      if (b->next)
        b->next->prev = b->prev;
      if (b->prev)
        b->prev->next = b->next;
      else
        bot->blocks = b->next;
      
      free(b->blockstr);
      free(b);
      
      bot->block_count--;
      
      sendnoticetouser(bot->np, sender, "Block removed.");
      return CMD_OK;
    }
  }
  
  sendnoticetouser(bot->np, sender, "No such block.");
  
  return CMD_ERROR;
}
