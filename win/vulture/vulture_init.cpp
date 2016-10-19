/* NetHack may be freely redistributed.  See license for details. */

#include <vector>
#include <string>


#include <errno.h>

#include "vulture_win.h"
#include "vulture_gra.h"
#include "vulture_gen.h"
#include "vulture_sdl.h"
#include "vulture_gfl.h"
#include "vulture_txt.h"
#include "vulture_init.h"
#include "vulture_nhplayerselection.h"
#include "vulture_main.h"
#include "vulture_sound.h"
#include "vulture_mou.h"
#include "vulture_tile.h"
#include "vulture_opt.h"

#include "winclass/introwin.h"
#include "vendor/theoraplay/theoraplay.h"

#include "date.h" /* this is in <variant>/include it's needed for VERSION_ID */

static void vulture_show_intro(std::string introscript_name);


/*----------------------------
* function implementaions
*---------------------------- */
static Uint32 baseticks = 0;

typedef struct AudioQueue
{
    const THEORAPLAY_AudioPacket *audio;
    int offset;
    struct AudioQueue *next;
} AudioQueue;

static volatile AudioQueue *audio_queue = NULL;
static volatile AudioQueue *audio_queue_tail = NULL;

static void vulture_show_darkarts_intro_queue_audio(const THEORAPLAY_AudioPacket *audio)
{
    AudioQueue *item = (AudioQueue *) malloc(sizeof (AudioQueue));
    if (!item)
    {
        THEORAPLAY_freeAudio(audio);
        return;  // oh well.
    } // if

    item->audio = audio;
    item->offset = 0;
    item->next = NULL;

    SDL_LockAudio();
    if (audio_queue_tail)
        audio_queue_tail->next = item;
    else
        audio_queue = item;
    audio_queue_tail = item;
    SDL_UnlockAudio();
}

static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len)
{
    // !!! FIXME: this should refuse to play if item->playms is in the future.
    //const Uint32 now = SDL_GetTicks() - baseticks;
    Sint16 *dst = (Sint16 *) stream;

    while (audio_queue && (len > 0))
    {
        volatile AudioQueue *item = audio_queue;
        AudioQueue *next = item->next;
        const int channels = item->audio->channels;

        const float *src = item->audio->samples + (item->offset * channels);
        int cpy = (item->audio->frames - item->offset) * channels;
        int i;

        if (cpy > (len / sizeof (Sint16)))
            cpy = len / sizeof (Sint16);

        for (i = 0; i < cpy; i++)
        {
            const float val = *(src++);
            if (val < -1.0f)
                *(dst++) = -32768;
            else if (val > 1.0f)
                *(dst++) = 32767;
            else
                *(dst++) = (Sint16) (val * 32767.0f);
        } // for

        item->offset += (cpy / channels);
        len -= cpy * sizeof (Sint16);

        if (item->offset >= item->audio->frames)
        {
            THEORAPLAY_freeAudio(item->audio);
            free((void *) item);
            audio_queue = next;
        } // if
    } // while

    if (!audio_queue)
        audio_queue_tail = NULL;

    if (len > 0)
        memset(dst, '\0', len);
} // audio_callback


void vulture_show_darkarts_intro(void)
{
  THEORAPLAY_Decoder *decoder = NULL;
  const THEORAPLAY_VideoFrame *video = NULL;
  const THEORAPLAY_AudioPacket *audio = NULL;

  SDL_Overlay *overlay = NULL;
  SDL_Event event;
  SDL_AudioSpec spec;

  Uint32 framems = 0;

  std::string fname;
  int quit = 0;
  
  fname = vulture_make_filename(V_MOVIE_DIRECTORY, "", "darkarts.ogv");

  decoder = THEORAPLAY_startDecodeFile(fname.c_str(), 30, THEORAPLAY_VIDFMT_IYUV);
  if (!decoder)
  {
      fprintf(stderr, "Failed to start decoding '%s'!\n", fname.c_str());
      return;
  }

  while (!audio || !video)
  {
      if (!audio) audio = THEORAPLAY_getAudio(decoder);
      if (!video) video = THEORAPLAY_getVideo(decoder);
      SDL_Delay(10);
  }

  SDL_FillRect(vulture_screen, NULL, SDL_MapRGB(vulture_screen->format, 0, 0, 0));
  SDL_Flip(vulture_screen);

  overlay = SDL_CreateYUVOverlay(video->width, video->height,SDL_IYUV_OVERLAY, vulture_screen);

  if (!overlay)
  {
    fprintf(stderr, "YUV Overlay failed: %s\n", SDL_GetError());
    return;
  }

  memset(&spec, '\0', sizeof (SDL_AudioSpec));
  spec.freq = audio->freq;
  spec.format = AUDIO_S16SYS;
  spec.channels = audio->channels;
  spec.samples = 2048;
  spec.callback = audio_callback;
  int initfailed =(SDL_OpenAudio(&spec, NULL) != 0);

  if (initfailed)
  {
    fprintf(stderr, "Audio Init Failed: %s\n", SDL_GetError());
    return;
  }

  while (audio)
  {
      vulture_show_darkarts_intro_queue_audio(audio);
      audio = THEORAPLAY_getAudio(decoder);
  }

  baseticks = SDL_GetTicks();
  SDL_PauseAudio(0);

  SDL_FillRect( vulture_screen, NULL, SDL_MapRGB( vulture_screen->format, 0, 0, 0 ));
  SDL_Flip( vulture_screen );

  while (!quit && THEORAPLAY_isDecoding(decoder))
  {
      const Uint32 now = SDL_GetTicks() - baseticks;
  
      if (!video)
          video = THEORAPLAY_getVideo(decoder);
  
      // Play video frames when it's time.
      if (video && (video->playms <= now))
      {
          //printf("Play video frame (%u ms)!\n", video->playms);
          if ( framems && ((now - video->playms) >= framems) )
          {
              // Skip frames to catch up, but keep track of the last one
              //  in case we catch up to a series of dupe frames, which
              //  means we'd have to draw that final frame and then wait for
              //  more.
              const THEORAPLAY_VideoFrame *last = video;
              while ((video = THEORAPLAY_getVideo(decoder)) != NULL)
              {
                  THEORAPLAY_freeVideo(last);
                  last = video;
                  if ((now - video->playms) < framems)
                      break;
              } // while
  
              if (!video)
                  video = last;
          } // if
  
          if (!video)  // do nothing; we're far behind and out of options.
          {
              static int warned = 0;
              if (!warned)
              {
                  warned = 1;
                  fprintf(stderr, "WARNING: Playback can't keep up!\n");
              } // if
          } // if
  
          else if (SDL_LockYUVOverlay(overlay) == -1)
          {
              static int warned = 0;
              if (!warned)
              {
                  warned = 1;
                  fprintf(stderr, "Couldn't lock YUV overlay: %s\n", SDL_GetError());
              } // if
          } // else if
  
          else
          {
              SDL_Rect dstrect = { 0, 0, vulture_screen->w, vulture_screen->h };
              const int w = video->width;
              const int h = video->height;
              const Uint8 *y = (const Uint8 *) video->pixels;
              const Uint8 *u = y + (w * h);
              const Uint8 *v = u + ((w/2) * (h/2));
              Uint8 *dst;
              int i;
  
              dst = overlay->pixels[0];
              for (i = 0; i < h; i++, y += w, dst += overlay->pitches[0])
                  memcpy(dst, y, w);
  
              dst = overlay->pixels[1];
              for (i = 0; i < h/2; i++, u += w/2, dst += overlay->pitches[1])
                  memcpy(dst, u, w/2);
  
              dst = overlay->pixels[2];
              for (i = 0; i < h/2; i++, v += w/2, dst += overlay->pitches[1])
                  memcpy(dst, v, w/2);
  
              SDL_UnlockYUVOverlay(overlay);
  
              if (SDL_DisplayYUVOverlay(overlay, &dstrect) != 0)
              {
                  static int warned = 0;
                  if (!warned)
                  {
                      warned = 1;
                      fprintf(stderr, "Couldn't display YUV overlay: %s\n", SDL_GetError());
                  } // if
              } // if
          } // else
  
          THEORAPLAY_freeVideo(video);
          video = NULL;
      } // if
      else  // no new video frame? Give up some CPU.
      {
          SDL_Delay(10);
      } // else
  
      while ((audio = THEORAPLAY_getAudio(decoder)) != NULL)
          vulture_show_darkarts_intro_queue_audio(audio);
  
      // Pump the event loop here.
      while (SDL_PollEvent(&event))
      {
          switch (event.type)
          {
              case SDL_VIDEOEXPOSE:
                  if (overlay)
                  {
                      SDL_Rect dstrect = { 0, 0, vulture_screen->w, vulture_screen->h };
                      SDL_DisplayYUVOverlay(overlay, &dstrect);
                      SDL_Flip( vulture_screen );
                  } // if
                  break;
  
              case SDL_QUIT:
                  quit = 1;
                  break;
  
              case SDL_KEYDOWN:
                  quit = 1;
                  break;
          } // switch
      } // while
  } // while

  SDL_CloseAudio();
}


void vulture_player_selection(void)
{
	SDL_Surface *logo;
  std::string filename;

	SDL_FillRect(vulture_screen, NULL, CLR32_BLACK);
	logo = vulture_load_graphic(V_FILENAME_CHARACTER_GENERATION);
	if (logo != NULL) {
		vulture_put_img((vulture_screen->w - logo->w) / 2, (vulture_screen->h - logo->h) / 2, logo);
		SDL_FreeSurface(logo);
	}

	vulture_fade_in(0.2);

	vulture_player_selection_internal();

	/* Success! Show introduction. */
	vulture_fade_out(0.2);

	filename = vulture_make_filename(V_CONFIG_DIRECTORY, "", V_FILENAME_INTRO_SCRIPT);

	if (flags.legacy)
		vulture_show_intro(filename);
}


void vulture_askname(void)
{
	int done;
	char inputbuffer[256];

	done = vulture_get_input(-1, vulture_screen->h - 170,
						"What is your name?", inputbuffer);
	if (!done)
		/* Player pressed ESC during the name query, so quit the game */
		vulture_bail(NULL);

	strncpy(plname, inputbuffer, 32);

	/* do not fade out if user didn't enter anything, we will come back here */
	if (plname[0])
		vulture_fade_out(0.2);
}


static void vulture_show_intro(std::string introscript_name)
{
	FILE *f;
	char buffer[1024];
	unsigned int nr_scenes, lineno;
  std::string line;
  std::vector<std::string> imagenames;
  std::vector< std::vector<std::string> > subtitles;
	introwin *iw;
    int dummy;

	/* read intro script */
	f = fopen(introscript_name.c_str(), "rb");
	if (f == NULL) {
		vulture_write_log(V_LOG_NOTE, NULL, 0, "intro script %s not found\n",
		                   introscript_name.c_str());
		return;
	}
	
	lineno = nr_scenes = 0;
	while (fgets(buffer, 1024, f)) {
		lineno++;
		
		if (buffer[0] == '%') {
			/* new scene */
			line = std::string(&buffer[1]);
			trim(line);
			
			if (line.length() == 0)
				continue;
			
			nr_scenes++;
			
			if (line.substr(line.length() - 4, 4) != ".png")
				vulture_write_log(V_LOG_NOTE, NULL, 0, "scene image %s not png?", line.c_str());
			
			/* trim off the file extension */
			line = line.substr(0, line.length() - 4);
			
			imagenames.push_back(line);
			subtitles.resize(nr_scenes);
			
		} else {
			/* text lines for current scene */
			if (nr_scenes == 0) {
				vulture_write_log(V_LOG_NOTE, NULL, 0, "subtitle without a preceding scene in line %d of intro script %s\n", lineno, introscript_name.c_str());
				continue;
			}
			
			line = std::string(buffer);
			trim(line);
			
			subtitles[nr_scenes - 1].push_back(line);
		}
	}

	if (nr_scenes == 0) {
		vulture_write_log(V_LOG_NOTE, NULL, 0, "no scenes found in intro script %s\n",
		                   introscript_name.c_str());
		return;
	}
	
	/* display intro */
	iw = new introwin(NULL, imagenames, subtitles);
	vulture_event_dispatcher(&dummy, V_RESPOND_INT, iw);
	delete iw;
}


static void vulture_init_colors()
{
	/* set up the colors used in the game
	* the only good way to do this without needing graphics to have been loaded first
	* is to create a surface here which we then put into display format + alpha ourselves */
	SDL_Surface * pixel = SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_SRCALPHA, 1, 1, 32,
								0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
	SDL_Surface * reformatted = SDL_DisplayFormatAlpha(pixel);

	vulture_px_format = new SDL_PixelFormat;
	memcpy(vulture_px_format, reformatted->format, sizeof(SDL_PixelFormat));

	SDL_FreeSurface(reformatted);
	SDL_FreeSurface(pixel);

	CLR32_BLACK      = SDL_MapRGBA(vulture_px_format, 0,0,0, 0xff);
	CLR32_BLACK_A30  = SDL_MapRGBA(vulture_px_format, 0,0,0, 0x50);
	CLR32_BLACK_A50  = SDL_MapRGBA(vulture_px_format, 0,0,0, 0x80);
	CLR32_BLACK_A70  = SDL_MapRGBA(vulture_px_format, 0,0,0, 0xB0);
	CLR32_GREEN      = SDL_MapRGBA(vulture_px_format, 0x57, 0xff, 0x57, 0xff);
	CLR32_YELLOW     = SDL_MapRGBA(vulture_px_format, 0xff, 0xff, 0x57, 0xff);
	CLR32_ORANGE     = SDL_MapRGBA(vulture_px_format, 0xff, 0xc7, 0x3b, 0xff);
	CLR32_RED        = SDL_MapRGBA(vulture_px_format, 0xff, 0x23, 0x07, 0xff);
	CLR32_GRAY20     = SDL_MapRGBA(vulture_px_format, 0xb7, 0xab, 0xab, 0xff);
	CLR32_GRAY70     = SDL_MapRGBA(vulture_px_format, 0x53, 0x53, 0x53, 0xff);
	CLR32_GRAY77     = SDL_MapRGBA(vulture_px_format, 0x43, 0x3b, 0x3b, 0xff);
	CLR32_PURPLE44   = SDL_MapRGBA(vulture_px_format, 0x4f, 0x43, 0x6f, 0xff);
	CLR32_LIGHTPINK  = SDL_MapRGBA(vulture_px_format, 0xcf, 0xbb, 0xd3, 0xff);
	CLR32_LIGHTGREEN = SDL_MapRGBA(vulture_px_format, 0xaa, 0xff, 0xcc, 0xff);
	CLR32_BROWN      = SDL_MapRGBA(vulture_px_format, 0x9b, 0x6f, 0x57, 0xff);
	CLR32_WHITE      = SDL_MapRGBA(vulture_px_format, 0xff, 0xff, 0xff, 0xff);
	CLR32_BLESS_BLUE = SDL_MapRGBA(vulture_px_format, 0x96, 0xdc, 0xfe, 0x60);
	CLR32_CURSE_RED  = SDL_MapRGBA(vulture_px_format, 0x60, 0x00, 0x00, 0x50);
	CLR32_GOLD_SHADE = SDL_MapRGBA(vulture_px_format, 0xf0, 0xe0, 0x57, 0x40);
}


int vulture_init_graphics(void)
{
	int all_ok = TRUE;
	SDL_Surface *image;
  std::string fullname;
	int font_loaded = 0;


	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Initializing filenames\n");
	vulture_init_gamepath();


	/* Read options file */
	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Reading Vulture's options\n");
	vulture_read_options();


	/* Enter graphics mode */
	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Enter graphics mode\n");
	vulture_enter_graphics_mode();


	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Initializing screen buffer\n");


	vulture_init_colors();

	/* Load fonts */
	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Initializing fonts\n");

	/* try custom font first */
	if (iflags.wc_font_text) {
		if (access(iflags.wc_font_text, R_OK) == 0) {
			font_loaded = 1;
			font_loaded &= vulture_load_font(V_FONT_SMALL, iflags.wc_font_text, 0, 12);
			font_loaded &= vulture_load_font(V_FONT_LARGE, iflags.wc_font_text, 0, 14);
		}
		else
			printf("Could not access %s: %s\n", iflags.wc_font_text, strerror(errno));
	}

	if (!font_loaded) {/* fallback to default font */
		font_loaded = 1;
		/* add the path to the filename */
		fullname = vulture_make_filename(V_FONTS_DIRECTORY, "", V_FILENAME_FONT);

		font_loaded &= vulture_load_font(V_FONT_SMALL, fullname.c_str(), 0, 12);
		font_loaded &= vulture_load_font(V_FONT_LARGE, fullname.c_str(), 0, 14);
	}

	all_ok &= font_loaded;

	/* Load window style graphics */
	image = vulture_load_graphic(V_FILENAME_WINDOW_STYLE);
	if (image == NULL)
		all_ok = FALSE;
	else {
		vulture_winelem.corner_tl = vulture_get_img_src(1, 1, 23, 23, image);
		vulture_winelem.border_top = vulture_get_img_src(27, 1, 57, 23, image);
		vulture_winelem.corner_tr = vulture_get_img_src(61, 1, 84, 23, image);
		vulture_winelem.border_left = vulture_get_img_src(1, 27, 23, 54, image);
		vulture_winelem.center = vulture_get_img_src(141, 1, 238, 168, image);
		vulture_winelem.border_right = vulture_get_img_src(61, 27, 84, 54, image);
		vulture_winelem.corner_bl = vulture_get_img_src(1, 58, 23, 82, image);
		vulture_winelem.border_bottom = vulture_get_img_src(27, 58, 57, 82, image);
		vulture_winelem.corner_br = vulture_get_img_src(61, 58, 84, 82, image);
		vulture_winelem.checkbox_off = vulture_get_img_src(1, 107, 17, 123, image);
		vulture_winelem.checkbox_on = vulture_get_img_src(21, 107, 37, 123, image);
		vulture_winelem.checkbox_count = vulture_get_img_src(21, 127, 37, 143, image);
		vulture_winelem.radiobutton_off = vulture_get_img_src(41, 107, 57, 123, image);
		vulture_winelem.radiobutton_on = vulture_get_img_src(61, 107, 77, 123, image);
		vulture_winelem.scrollbar = vulture_get_img_src(81, 107, 97, 123, image);
		vulture_winelem.scrollbutton_down = vulture_get_img_src(101, 107, 117, 123, image);
		vulture_winelem.scrollbutton_up = vulture_get_img_src(121, 107, 137, 123, image);
		vulture_winelem.scroll_indicator = vulture_get_img_src(1, 127, 17, 154, image);
		vulture_winelem.direction_arrows = vulture_get_img_src(242, 1, 576, 134, image);
		vulture_winelem.invarrow_left = vulture_get_img_src(1, 158, 67, 174, image);
		vulture_winelem.invarrow_right = vulture_get_img_src(1, 178, 67, 194, image);
		vulture_winelem.closebutton = vulture_get_img_src(41, 127, 59, 145, image);
		SDL_FreeSurface(image);
	}

	/* Initialize tile bitmaps */
	vulture_load_gametiles();


	/* make sure the cursor is not NULL when we try to draw for the first time */
	vulture_mouse_init();

	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Vulture's window system ready.\n");

	vulture_set_draw_region(0, 0, vulture_screen->w, vulture_screen->h);
	SDL_FillRect(vulture_screen, NULL, CLR32_BLACK);

	return all_ok;
}



void vulture_destroy_graphics(void)
{
	unsigned int i;

	/* free tilearrays, related data */
	vulture_unload_gametiles();

	/* clean up fonts */
	vulture_free_fonts();

	/* unload mouse & tooltip backgrounds, tooltip text and tootltip surface */
	vulture_mouse_destroy();
	
	vulture_eventstack_destroy();

	/* free window elements */
	SDL_FreeSurface(vulture_winelem.corner_tl);
	SDL_FreeSurface(vulture_winelem.border_top);
	SDL_FreeSurface(vulture_winelem.corner_tr);
	SDL_FreeSurface(vulture_winelem.border_left);
	SDL_FreeSurface(vulture_winelem.center);
	SDL_FreeSurface(vulture_winelem.border_right);
	SDL_FreeSurface(vulture_winelem.corner_bl);
	SDL_FreeSurface(vulture_winelem.border_bottom);
	SDL_FreeSurface(vulture_winelem.corner_br);
	SDL_FreeSurface(vulture_winelem.checkbox_off);
	SDL_FreeSurface(vulture_winelem.checkbox_on);
	SDL_FreeSurface(vulture_winelem.checkbox_count);
	SDL_FreeSurface(vulture_winelem.radiobutton_off);
	SDL_FreeSurface(vulture_winelem.radiobutton_on);
	SDL_FreeSurface(vulture_winelem.scrollbar);
	SDL_FreeSurface(vulture_winelem.scrollbutton_down);
	SDL_FreeSurface(vulture_winelem.scrollbutton_up);
	SDL_FreeSurface(vulture_winelem.scroll_indicator);
	SDL_FreeSurface(vulture_winelem.direction_arrows);
	SDL_FreeSurface(vulture_winelem.invarrow_left);
	SDL_FreeSurface(vulture_winelem.invarrow_right);
	SDL_FreeSurface(vulture_winelem.closebutton);

	/* free sound descriptions */
	for (i = 0; i < vulture_event_sounds.size(); i++)
		free (vulture_event_sounds[i].searchpattern);

	/* misc small stuff */
	delete vulture_px_format;
}
