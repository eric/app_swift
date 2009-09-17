/*
 *  app_swift -- A Cepstral Swift TTS engine interface  
 *
 *  Copyright (C) 2008, Darren Sessions
 *  Copyright (C) 2006, Will Orton
 *
 *  Darren Sessions <dmsessions@gmail.com>
 *
 *
 *  This program is free software, distributed under the terms of
 *  the GNU General Public License Version 2. Read the LICENSE 
 *  file for details.
 *
 */

/*! \file
 *
 * \brief Cepstral Swift TTS engine interface
 *
 * \author Darren Sessions <dmsessions@gmail.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
        <defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"
ASTERISK_FILE_VERSION(__FILE__, "$Revision: 1.6.2 $")

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include <swift.h>

#include "asterisk/astobj.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/options.h"
#include "asterisk/time.h"
#include "asterisk/app.h"

static char *tdesc = "Swift TTS Application";

static char *app = "Swift";

static char *synopsis = "Speak text through Swift text-to-speech engine.";

static char *descrip =" Syntax: Swift(text[|timeout in ms|maximum digits])\n"
                      "Example: Swift(Hello World|5000|5) = 5 second delay between 5 digits\n"
                      "This application operates in two modes. One is processing text-to-speech while\n"
                      "listening for DTMF and the other just processes the text-to-speech while ignoring\n"
                      "DTMF entirely. \n"
                      "Unless the timeout and maximum digits options are BOTH specified, the application\n"
                      "will automatically ignore DTMF.\n"
                      "Returns -1 on hangup or 0 otherwise.  \n";

#define AST_MODULE "app_swift"

const int framesize = 160 * 4;
const int samplerate = 8000; // We do uLAW

#define SWIFT_CONFIG_FILE "swift.conf"
static unsigned int cfg_buffer_size;
static int cfg_goto_exten;
static char cfg_voice[20];

// Data shared between is and the swift generating process
struct stuff {
    ASTOBJ_COMPONENTS(struct stuff);
    int generating_done;
    char *q;
    char *pq_r;  //queue read position
    char *pq_w;  //queue write position
    int qc;
    int immediate_exit;
  
};


#define dtmf_codes 12

struct dtmf_lookup
{
    long ast_res;
    char* dtmf_res;
};

static struct dtmf_lookup ast_dtmf_table[dtmf_codes] =
{
    {35, "#"},
    {42, "*"},
    {48, "0"},
    {49, "1"},
    {50, "2"},
    {51, "3"},
    {52, "4"},
    {53, "5"},
    {54, "6"},
    {55, "7"},
    {56, "8"},
    {57, "9"}
};

void swift_init_stuff(struct stuff *ps)
{
    ASTOBJ_INIT(ps);
    ps->generating_done = 0;
    ps->q = malloc(cfg_buffer_size);
    ps->pq_r = ps->q;
    ps->pq_w = ps->q;
    ps->qc = 0;
    ps->immediate_exit = 0;
}

// Returns true if swift is generating speech or we still have some
// queued up.
int swift_generator_running(struct stuff *ps)
{
    int r;
    ASTOBJ_RDLOCK(ps);
    r = !ps->immediate_exit && (!ps->generating_done || ps->qc);
    ASTOBJ_UNLOCK(ps);
    return r;
}

int swift_bytes_available(struct stuff *ps)
{
    int r;
    ASTOBJ_RDLOCK(ps);
    r = ps->qc;
    ASTOBJ_UNLOCK(ps);
    return r;
}

swift_result_t swift_cb(swift_event *event, swift_event_t type, void *udata)
{
    void *buf;
    int len, spacefree;
    unsigned long sleepfor;
    swift_event_t rv = SWIFT_SUCCESS;
    struct stuff *ps = udata;

    if (type == SWIFT_EVENT_AUDIO) {
        rv = swift_event_get_audio(event, &buf, &len);
        if (!SWIFT_FAILED(rv) && len > 0) {
//            ast_log(LOG_DEBUG, "audio callback\n");

            ASTOBJ_WRLOCK(ps);

            // Sleep while waiting for some queue space to become available
            while (len + ps->qc > cfg_buffer_size && !ps->immediate_exit) {
                // Each byte is 125us of time, so assume queue space will become available
                // at that rate and guess when we'll have enough space available.
                // + another (125 usec/sample * framesize samples) (1 frame) for fudge
                sleepfor = ((unsigned long)(len - (cfg_buffer_size - ps->qc)) * 125UL) + (125UL * (unsigned long)framesize);
//                ast_log(LOG_DEBUG, "generator: %d bytes to write but only %d space avail, sleeping %ldus\n", len, cfg_buffer_size - ps->qc, sleepfor);
                ASTOBJ_UNLOCK(ps);
                usleep(sleepfor);
                ASTOBJ_WRLOCK(ps);
            }

            if (ps->immediate_exit)
                return SWIFT_SUCCESS;
            
            spacefree = cfg_buffer_size - ((uintptr_t) ps->pq_w - (uintptr_t)ps->q);
            if (len > spacefree) {
//                ast_log(LOG_DEBUG, "audio fancy write; %d bytes but only %d avail to end %d totalavail\n", len, spacefree, cfg_buffer_size - ps->qc);
                //write #1 to end of mem
                memcpy(ps->pq_w, buf, spacefree);
                ps->pq_w = ps->q;
                ps->qc += spacefree;

                //write #2 and beg of mem
                memcpy(ps->pq_w, buf + spacefree, len - spacefree);
                ps->pq_w += len - spacefree;
                ps->qc += len - spacefree;
      
            } else {
//                ast_log(LOG_DEBUG, "audio easy write, %d avail to end %d totalavail\n", spacefree, cfg_buffer_size - ps->qc);
                memcpy(ps->pq_w, buf, len);
                ps->pq_w += len;
                ps->qc += len;
            }
            ASTOBJ_UNLOCK(ps);
        } else {
            ast_log(LOG_DEBUG, "got audio callback but get_audio call failed\n");
        }
            
    } else if (type == SWIFT_EVENT_END) {
        ast_log(LOG_DEBUG, "got END callback; done generating audio\n");
        ASTOBJ_WRLOCK(ps);
        ps->generating_done = 1;
        ASTOBJ_UNLOCK(ps);
    } else {
        ast_log(LOG_DEBUG, "UNKNOWN callback\n");
    }
    return rv;
}

static int dtmf_conv(int dtmf)
{
    char *res = (char *) malloc(100); 
    int dtmf_search_counter = 0, dtmf_search_match = 0;

    while ((dtmf_search_counter < dtmf_codes) && (dtmf_search_match == 0)) {
      if (dtmf == ast_dtmf_table[dtmf_search_counter].ast_res) {
        dtmf_search_match = 1;
        sprintf(res, "%s", ast_dtmf_table[dtmf_search_counter].dtmf_res);
      }
      dtmf_search_counter = dtmf_search_counter + 1;
    }

    return *res;
}

static int listen_for_dtmf(struct ast_channel *chan, int timeout, int max_digits) 
{
    char *dtmf_conversion = (char *) malloc(100);
    char cnv[2];
    int dtmf, res;
    int i = 0, loop = 0;

    while (i < max_digits && loop == 0) {

      dtmf = ast_waitfordigit(chan, 5000);

      if (dtmf) {
        sprintf(cnv, "%c", dtmf_conv(dtmf));
        if (i > 0) {
          strcat(dtmf_conversion, cnv);
        } else {
          sprintf(dtmf_conversion, "%s", cnv);
        }
        i = i + 1;
      } else {
        loop = 1;
      }
    }

    res = strtol(dtmf_conversion, NULL, 0);

    return res;
}

static int engine(struct ast_channel *chan, void *data)
{
    int res = 0, argc = 0, max_digits = 0, timeout = 0, alreadyran = 0;
    int ms, len, old_writeformat, availatend, rc;
    char *argv[3], *parse = NULL, *text = NULL, results[20], tmp_exten[2];
    struct ast_module_user *u;
    struct ast_frame *f;
    struct myframe {
        struct ast_frame f;
        unsigned char offset[AST_FRIENDLY_OFFSET];
        unsigned char frdata[framesize];
    } myf;
    struct timeval next;

    // Swift TTS engine stuff
    swift_engine *engine;
    swift_port *port;
    swift_voice *voice;
    swift_params *params;
    swift_result_t sresult;
    swift_background_t tts_stream;
    unsigned int event_mask;

    struct stuff *ps;

    parse = ast_strdupa(data); 

    u = ast_module_user_add(chan);
   
    argc = ast_app_separate_args(parse, '|', argv, sizeof(argv) / sizeof(argv[0]));

    text = argv[0];
    
    if (!ast_strlen_zero(argv[1])) 
      timeout    = strtol(argv[1], NULL, 0);
  
    if (!ast_strlen_zero(argv[2]))
      max_digits = strtol(argv[2], NULL, 0);

    if (ast_strlen_zero(text)) {
        ast_log(LOG_WARNING, "%s requires text to speak!\n", app);
        return -1;
    }

    if (!ast_strlen_zero(text))
            ast_log(LOG_NOTICE, "Text to Speak : %s\n", text);

    if (timeout > 0)
      ast_log(LOG_NOTICE, "Timeout : %d\n", timeout);

    if (max_digits > 0)
      ast_log(LOG_NOTICE, "Max Digits : %d\n", max_digits);

    ps = malloc(sizeof(struct stuff));
    swift_init_stuff(ps);

    //// Set up synthesis

    if((engine = swift_engine_open(NULL)) == NULL) {
        ast_log(LOG_ERROR, "Failed to open Swift Engine.\n");
        goto exception;
    }

    params = swift_params_new(NULL);
    swift_params_set_string(params, "audio/encoding", "ulaw");
    swift_params_set_string(params, "audio/sampling-rate", "8000");
    swift_params_set_string(params, "audio/output-format", "raw");
    swift_params_set_string(params, "tts/text-encoding", "utf-8");
//    swift_params_set_float(params, "speech/pitch/shift", 1.0);
//    swift_params_set_int(params, "speech/rate", 150);
//    swift_params_set_int(params, "audio/volume", 110);
    swift_params_set_int(params, "audio/deadair", 0);

    if((port = swift_port_open(engine, params)) == NULL) {
        ast_log(LOG_ERROR, "Failed to open Swift Port.\n");
        goto exception;
    }

    if ((voice = swift_port_set_voice_by_name(port, cfg_voice)) == NULL) {
        ast_log(LOG_ERROR, "Failed to set voice.\n");
        goto exception;
    }

    event_mask = SWIFT_EVENT_AUDIO | SWIFT_EVENT_END;
    swift_port_set_callback(port, &swift_cb, event_mask, ps);

    if(SWIFT_FAILED(swift_port_speak_text(port, text, 0, NULL, &tts_stream, NULL))) {
        ast_log(LOG_ERROR, "Failed to speak.\n");
        goto exception;
    }

    if(chan->_state!=AST_STATE_UP)
        ast_answer(chan);
    ast_stopstream(chan);

    old_writeformat = chan->writeformat;
    if(ast_set_write_format(chan, AST_FORMAT_ULAW) < 0) {
        ast_log(LOG_WARNING, "Unable to set write format.\n");
        goto exception;
    }

    res = 0;
    // Wait 100 ms first for synthesis to start crankin'; if that's not
    // enough the 
    next = ast_tvadd(ast_tvnow(), ast_tv(0, 100000));

    while (swift_generator_running(ps)) {
        ms = ast_tvdiff_ms(next, ast_tvnow());
        if (ms <= 0) {
            if (swift_bytes_available(ps) > 0) {
                ASTOBJ_WRLOCK(ps);
//                ast_log(LOG_DEBUG, "Queue %d bytes, writing a frame\n", ps->qc);

                len = fmin(framesize, ps->qc);
                availatend = cfg_buffer_size - (ps->pq_r - ps->q);
                if (len > availatend) {
//                    ast_log(LOG_DEBUG, "Fancy read; %d bytes but %d at end, %d free \n", len, availatend, cfg_buffer_size - ps->qc);

                    //read #1: to end of q buf
                    memcpy(myf.frdata, ps->pq_r, availatend);
                    ps->qc -= availatend;

                    //read #2: reset to start of q buf and get rest
                    ps->pq_r = ps->q;
                    memcpy(myf.frdata + availatend, ps->pq_r, len - availatend);
                    ps->qc -= len - availatend;
                    ps->pq_r += len - availatend;

                } else {
//                    ast_log(LOG_DEBUG, "Easy read; %d bytes and %d at end, %d free\n", len, availatend, cfg_buffer_size - ps->qc);
                    memcpy(myf.frdata, ps->pq_r, len);
                    ps->qc -= len;
                    ps->pq_r += len;
                }
                
                myf.f.frametype = AST_FRAME_VOICE;
                myf.f.subclass = AST_FORMAT_ULAW;
                myf.f.datalen = len;
                myf.f.samples = len;
                myf.f.data.ptr = myf.frdata;
                myf.f.mallocd = 0;
                myf.f.offset = AST_FRIENDLY_OFFSET;
                myf.f.src = __PRETTY_FUNCTION__;
                myf.f.delivery.tv_sec = 0;
                myf.f.delivery.tv_usec = 0;
                if(ast_write(chan, &myf.f) < 0) {
                    ast_log(LOG_DEBUG, "ast_write failed\n");
                }
//                ast_log(LOG_DEBUG, "wrote a frame of %d\n", len);
                if (ps->qc < 0)
                    ast_log(LOG_DEBUG, "queue claims to contain negative bytes. Huh? qc < 0\n");
                ASTOBJ_UNLOCK(ps);
                next = ast_tvadd(next, ast_samp2tv(myf.f.samples, samplerate));
                
            } else {
                next = ast_tvadd(next, ast_samp2tv(framesize/2, samplerate));
                ast_log(LOG_DEBUG, "Whoops, writer starved for audio\n");
            }
                
        } else {
            ms = ast_waitfor(chan, ms);
            if (ms < 0) {
                ast_log(LOG_DEBUG, "Hangup detected\n");
                res = -1;
                ASTOBJ_WRLOCK(ps);
                ps->immediate_exit = 1;
                ASTOBJ_UNLOCK(ps);
                
            } else if (ms) {
                f = ast_read(chan);
                if(!f) {
                    ast_log(LOG_DEBUG, "Null frame == hangup() detected\n");
                    res = -1;
                    ASTOBJ_WRLOCK(ps);
                    ps->immediate_exit = 1;
                    ASTOBJ_UNLOCK(ps);
                    
                } else if (f->frametype == AST_FRAME_DTMF && timeout > 0 && max_digits > 0) {
 
                    alreadyran = 1;
                    res = 0;

                    ASTOBJ_WRLOCK(ps);
                    ps->immediate_exit = 1;
                    ASTOBJ_UNLOCK(ps);

                    if (max_digits > 1) {
                      rc = listen_for_dtmf(chan, timeout, max_digits - 1);
                    }
                    
                    if (rc) {
                      sprintf(results, "%c%d", f->subclass, rc);
                    } else {
                      sprintf(results, "%c", f->subclass);
                    }

                    ast_log(LOG_NOTICE, "DTMF = %s\n", results);
                    pbx_builtin_setvar_helper(chan, "SWIFT_DTMF", results);
     
                    ast_frfree(f);
                    
                } else { // ignore other frametypes
                    ast_frfree(f);
                }
            } 
        }
 
        ASTOBJ_RDLOCK(ps);
        if (ps->immediate_exit && !ps->generating_done) {
            if (SWIFT_FAILED(sresult = swift_port_stop(port, tts_stream, SWIFT_EVENT_NOW))) {
                ast_log(LOG_NOTICE, "Early top of swift port failed\n");
            } else {
                ast_log(LOG_DEBUG, "Early stop of swift port returned okay\n");
            }
        }
        ASTOBJ_UNLOCK(ps);
    }

    if (alreadyran == 0 && timeout > 0 && max_digits > 0) {
      rc = listen_for_dtmf(chan, timeout, max_digits);
      if (rc) {
        sprintf(results, "%d", rc);
        ast_log(LOG_NOTICE, "DTMF = %s\n", results);
        pbx_builtin_setvar_helper(chan, "SWIFT_DTMF", results); 
      } else {
        ast_log(LOG_NOTICE, "No DTMF\n");
      }
    }

    if (max_digits == 1 && rc) {
      if (cfg_goto_exten) {
        if (ast_exists_extension (chan, chan->context, results, 1, chan->cid.cid_num)) {
          strncpy(chan->exten, tmp_exten, sizeof(chan->exten) - 1);
          chan->priority = 0;
        }
      }
    }


exception:
    if(port!=NULL) swift_port_close(port);
    if(engine!=NULL) swift_engine_close(engine);

    if (ps && ps->q) {
        free(ps->q);
        ps->q = NULL;
    }
    if (ps) {
        free(ps);
        ps = NULL;
    }

    if (!res && old_writeformat)
        ast_set_write_format(chan, old_writeformat);

    ast_module_user_remove(u);
    return res;
}

static int unload_module(void)
{
    int res;
   
    res = ast_unregister_application(app);

    ast_module_user_hangup_all();

    return res;
}

static int load_module(void)
{
    int res;
    const char *t = NULL;
    struct ast_config *cfg;
    struct ast_flags config_flags = { 0 };

    // Set defaults
    cfg_buffer_size = 65535;
    cfg_goto_exten = 0;
    strncpy(cfg_voice, "David-8kHz", sizeof(cfg_voice));
               
    res = ast_register_application(app, engine, synopsis, descrip);
    cfg = ast_config_load(SWIFT_CONFIG_FILE, config_flags);

    if (cfg) {
        if ((t = ast_variable_retrieve(cfg, "general", "buffer_size"))) {
            cfg_buffer_size = atoi(t);
            ast_log(LOG_DEBUG, "Config buffer_size is %d\n", cfg_buffer_size);
        }
        if ((t = ast_variable_retrieve(cfg, "general", "goto_exten"))) {
            if (!strcmp(t, "yes"))
                cfg_goto_exten = 1;
            else
                cfg_goto_exten = 0;
            ast_log(LOG_DEBUG, "Config goto_exten is %d\n", cfg_goto_exten);
        }

        if ((t = ast_variable_retrieve(cfg, "general", "voice"))) {
            strncpy(cfg_voice, t, sizeof(cfg_voice));
            ast_log(LOG_DEBUG, "Config voice is %s\n", cfg_voice);
        }

        ast_config_destroy(cfg);

    } else {
        ast_log(LOG_NOTICE, "Failed to load config\n");
    }

    return res;
}

char *description(void)
{
    return tdesc;
}

#define AST_MODULE "app_swift"

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Cepstral Swift TTS Application");
