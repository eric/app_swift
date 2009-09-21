
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __Darwin__
#include <swift/swift.h>
#else
#include <swift.h>
#endif

typedef struct  cst_wave_struct {
  const char *type;
  int sample_rate;
  int num_samples;
  int num_channels;
  short *samples;
} cst_wave;

int main(int argc, char *argv[])
{
    swift_engine *engine;
    swift_port *port;
    swift_voice *voice;
    swift_params *params;
    swift_result_t rv;
    swift_background_t tts_stream;

    if((engine = swift_engine_open(NULL)) == NULL) {
      fprintf(stderr, "Failed to open Swift Engine.\n");
      goto exception;
    }
    params = swift_params_new(NULL);
    swift_params_set_string(params, "audio/encoding", "pcm16");
    swift_params_set_string(params, "audio/sampling-rate", "8000");
    swift_params_set_string(params, "audio/output-format", "riff"); // raw
    swift_params_set_string(params, "audio/output-file", "-");
    swift_params_set_string(params, "tts/text-encoding", "utf-8");

    if((port = swift_port_open(engine, params)) == NULL) {
      fprintf(stderr, "Failed to open Swift Port.\n");
      goto exception;
    }
		//if((voice = swift_port_find_first_voice(port, "speaker/gender=female;language/tag=de-DE", NULL)) == NULL) {
		if((voice = swift_port_find_first_voice(port, "language/tag=de", NULL)) == NULL) {
			//if((voice = swift_port_find_first_voice(port, NULL, NULL)) == NULL) {
      fprintf(stderr, "Failed to find any voices!\n");
      goto exception;
    }
    if(SWIFT_FAILED(rv=swift_port_set_voice(port, voice))) {
      fprintf(stderr, "Failed to set voice. %s\n", swift_strerror(rv));
      goto exception;
    }
    
    if(SWIFT_FAILED(swift_port_speak_text(port, "hallo das ist ein text. was geht ab?", 0, NULL, &tts_stream, NULL))) {
      fprintf(stderr, "Failed to speak.\n");
      goto exception;
    }
    
 exception:
    if(port!=NULL) swift_port_close(port);
    if(engine!=NULL) swift_engine_close(engine);
    return 0;
}

