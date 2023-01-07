#include <GL/glut.h>    
#include <AR/gsub.h>    
#include <AR/video.h>   
#include <AR/param.h>   
#include <AR/ar.h>
#include <AR/arMulti.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <ao/ao.h>
#include <sndfile.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

#define MARKER_SIDE 50
#define PAPER_WIDTH 594.0
#define PAPER_HEIGHT 420.0
#define PAPER_HALF_WIDTH (PAPER_WIDTH / 2.0)
#define PAPER_HALF_HEIGHT (PAPER_HEIGHT / 2.0)
#define STEPS_NUMBER 8
#define BAR_WIDTH (PAPER_WIDTH / STEPS_NUMBER)
#define NOTE_HEIGHT (PAPER_HEIGHT / 7.0)
#define STEP_FREQUENCY 1000 /* 500 TODO CHANGE BACK */
#define CF_THRESHOLD 0.7
#define AUDIO_BUFFER_SIZE 8192
#define MAX_STRING_SIZE 1024

enum Class {
  NOTE,
  PIANO,
  MARIMBA,
  METRONOME
};

struct TObjectClass {
  int id;                // Identificador del patrón
  enum Class classCode;
  double width;          // Ancho del patron
  double center[2];      // Centro del patron  
  void (* drawme)(void); // Función de dibujado del patrón
};

struct TObjectInstance {
  double patt_trans[3][4];     // Matriz asociada a la instancia del patrón
  struct TObjectClass *class; // Clase de la instancia del patrón
};

struct TAudioSource {
    int64_t readBytes;
    int64_t targetBytes;
    SNDFILE *file;
    short *buffer;
};

struct TObjectClass *classes = NULL;
int nclasses = 0;
struct TObjectInstance *instances = NULL;
int ninstances = 0;

ARMultiMarkerInfoT  *mMarker;
int stepIndex = 0;
long long lastTimeChangedStep;

pthread_t audioThread;
sem_t audioSemaphore;
struct TAudioSource *audioSources = NULL;
int nAudioSources = 0;
ao_device *device;

// Note ranges from 0 = C3 = Do to 6 = B3 = Si
const char NOTE_NAME_MAPPING[7][3] = {"C3", "D3", "E3", "F3", "G3", "A3", "B3"};

void print_error (char *error) {  fprintf(stderr,"%s\n",error); exit(0); }

// Based on https://gist.github.com/maxammann/52d6b65b42d8ce23512a
void *play(void *parameter) {
    int64_t read;
    char *messageBuffer;
    SNDFILE *file;
    SF_INFO sfinfo;
    short *buffer = calloc(AUDIO_BUFFER_SIZE, sizeof(short));

    while (1) {
        for(int j=0; j<AUDIO_BUFFER_SIZE; j++) {
            buffer[j] = 0;
        }

        sem_wait(&audioSemaphore);
        for (int i=0; i<nAudioSources; i++) {
            read = sf_read_short(audioSources[i].file, audioSources[i].buffer, AUDIO_BUFFER_SIZE);
            audioSources[i].readBytes += read;
            if (read <= 0 || audioSources[i].readBytes >= audioSources[i].targetBytes) {
                nAudioSources--;
                for (int j=i; j<nAudioSources; j++) {
                    audioSources[j] = audioSources[j+1];
                }
                audioSources = realloc(audioSources, sizeof(struct TAudioSource)*nAudioSources);
                continue;
            }

            for (int j=0; j<read; j++) {
                // Calculating the average for mixing is an approximation (https://stackoverflow.com/a/16177281)
                buffer[j] += audioSources[i].buffer[j];
            }
        }

        if (nAudioSources <= 0) {
            usleep(100000);
            sem_post(&audioSemaphore);
            continue;
        }

        for (int j=0; j<AUDIO_BUFFER_SIZE; j++) {
            buffer[j] /= nAudioSources;
        }
        sem_post(&audioSemaphore);

        if (ao_play(device, (char *) buffer, (uint_32) (read * sizeof(short))) == 0) {
            fprintf(stderr, "ao_play: failed.\n");
            break;
        }
    }

    pthread_exit(NULL);
}

// ==== addClass (Anade objeto a la lista de objetos) ==============
void addClass(enum Class classCode, char *p, double w, double c[2], void (*drawme)(void)) {
  int pattid;

  if((pattid=arLoadPatt(p)) < 0) 
    print_error ("Error en carga de patron\n");

  nclasses++;
  classes = (struct TObjectClass *) 
    realloc(classes, sizeof(struct TObjectClass) * nclasses);

  classes[nclasses-1].classCode = classCode;
  classes[nclasses-1].id = pattid;
  classes[nclasses-1].width = w;
  classes[nclasses-1].center[0] = c[0];
  classes[nclasses-1].center[1] = c[1];
  classes[nclasses-1].drawme = drawme;
}

// ==== draw****** (Dibujado especifico de cada objeto) =============
void drawNote(void) {
  GLfloat material[]     = {0.0, 0.0, 1.0, 1.0};
  glMaterialfv(GL_FRONT, GL_AMBIENT, material);
  glTranslatef(0.0, 0.0, 20.0);
  glRotatef(90.0, 1.0, 0.0, 0.0);
  glutSolidTeapot(40.0);
}

// ======== cleanup =================================================
static void cleanup(void) {   // Libera recursos al salir ...
  arVideoCapStop();
  arVideoClose();
  argCleanup();
  pthread_cancel(audioThread);
  ao_close(device);
  ao_shutdown();
  exit(0);
}

// ======== keyboard ================================================
static void keyboard(unsigned char key, int x, int y) {
  switch (key) {
  case 0x1B: case 'Q': case 'q':  cleanup(); break;
  }
}

// https://stackoverflow.com/a/44896326
long long timeInMilliseconds() {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

static void updateState()  {
  long long newTime = timeInMilliseconds();
  if (newTime - lastTimeChangedStep > STEP_FREQUENCY) {
    lastTimeChangedStep = newTime;
    stepIndex = ++stepIndex % 8;

    SF_INFO sfinfo;
    short *buffer;
    SNDFILE *file;
    // Check for all entities in the new bar position
    double xMMark = mMarker->trans[0][3],
           yMMark = mMarker->trans[1][3];
    for (int i=0; i<ninstances; i++) {
      // TODO: avoid using the switch statement and use the TObjectClass
      switch (instances[i].class->classCode) {
        case NOTE:
        case PIANO:
        case MARIMBA:
          double x = instances[i].patt_trans[0][3] - xMMark,
                 y = instances[i].patt_trans[1][3] - yMMark;
          if (stepIndex*BAR_WIDTH < x && x < (stepIndex+1)*BAR_WIDTH && 0 < y && y < PAPER_HEIGHT) {
            int noteIndex = 6 - (int) (y/NOTE_HEIGHT); // TODO make notes fit within the inner rectangle
            sem_wait(&audioSemaphore);
            nAudioSources++;
            audioSources = realloc(audioSources, sizeof(struct TAudioSource) * nAudioSources);
            buffer = calloc(AUDIO_BUFFER_SIZE, sizeof(short));
            char *notePath = malloc(MAX_STRING_SIZE);
            snprintf(notePath, MAX_STRING_SIZE, "audio/piano/%s.wav", NOTE_NAME_MAPPING[noteIndex]);
            printf("%s\n", notePath); // TODO REMOVE
            file = sf_open(notePath, SFM_READ, &sfinfo);

            audioSources[nAudioSources-1].file = file;
            audioSources[nAudioSources-1].buffer = buffer; 
            audioSources[nAudioSources-1].readBytes = 0;
            audioSources[nAudioSources-1].targetBytes = (int) (0.5 * sfinfo.channels * sfinfo.samplerate);
            sem_post(&audioSemaphore);
          }
          break;
      }
    }
  }
}

// ======== draw ====================================================
static void draw( void ) {
  double  gl_para[16];   // Esta matriz 4x4 es la usada por OpenGL
  GLfloat barMaterial[]        = {1.0, 1.0, 1.0, 1.0};
  GLfloat light_position[]  = {100.0,-200.0,200.0,0.0};
  
  argDrawMode3D();              // Cambiamos el contexto a 3D
  argDraw3dCamera(0, 0);        // Y la vista de la camara a 3D
  glClear(GL_DEPTH_BUFFER_BIT); // Limpiamos buffer de profundidad
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  argConvGlpara(mMarker->trans, gl_para);   
  glMatrixMode(GL_MODELVIEW);           
  glLoadMatrixd(gl_para);               

  glEnable(GL_LIGHTING);  glEnable(GL_LIGHT0);
  glLightfv(GL_LIGHT0, GL_POSITION, light_position);
  
  // Draw bar
  glMaterialfv(GL_FRONT, GL_AMBIENT, barMaterial);
  glTranslatef(BAR_WIDTH*(stepIndex + 0.5), -1*PAPER_HALF_HEIGHT, 1.0);
  glScalef(BAR_WIDTH, PAPER_HEIGHT, 15);
  glutSolidCube(1);    

  for (int i=0; i<ninstances; i++) {
    argConvGlpara(instances[i].patt_trans, gl_para);   
    glMatrixMode(GL_MODELVIEW);           
    glLoadMatrixd(gl_para);

    glEnable(GL_LIGHTING);  glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    (*instances[i].class).drawme();
  }
  glDisable(GL_DEPTH_TEST);
}

// ======== init ====================================================
static void init( void ) {
  ARParam  wparam, cparam;   // Parametros intrinsecos de la camara
  int xsize, ysize;          // Tamano del video de camara (pixels)
  double c[2] = {0.0, 0.0};  // Centro de patrón (por defecto)
  
  // Abrimos dispositivo de video
  if(arVideoOpen("-dev=/dev/video2") < 0) exit(0);  
  if(arVideoInqSize(&xsize, &ysize) < 0) exit(0);

  // Cargamos los parametros intrinsecos de la camara
  if(arParamLoad("data/camera_para.dat", 1, &wparam) < 0)   
    print_error ("Error en carga de parametros de camara\n");
  
  arParamChangeSize(&wparam, xsize, ysize, &cparam);
  arInitCparam(&cparam);   // Inicializamos la camara con "cparam"

  // Cargamos el fichero de especificacion multimarca
  if( (mMarker = arMultiReadConfigFile("data/multimarker.dat")) == NULL )
    print_error("Error en fichero marker.dat\n");

  addClass(NOTE,      "data/note.patt",      MARKER_SIDE, c, drawNote); 
  addClass(PIANO,     "data/piano.patt",     MARKER_SIDE, c, drawNote); 
  addClass(MARIMBA,   "data/marimba.patt",   MARKER_SIDE, c, drawNote); 
  addClass(METRONOME, "data/metronome.patt", MARKER_SIDE, c, drawNote); 

  lastTimeChangedStep = timeInMilliseconds();
  argInit(&cparam, 1.0, 0, 0, 0, 0);   // Abrimos la ventana 
}

// ======== mainLoop ================================================
static void mainLoop(void) {
  ARUint8 *dataPtr;
  ARMarkerInfo *marker_info;
  int marker_num, i, j;

  // Capturamos un frame de la camara de video
  if((dataPtr = (ARUint8 *)arVideoGetImage()) == NULL) {
    // Si devuelve NULL es porque no hay un nuevo frame listo
    arUtilSleep(2);  return;  // Dormimos el hilo 2ms y salimos
  }

  argDrawMode2D();
  argDispImage(dataPtr, 0,0);    // Dibujamos lo que ve la camara 

  // Detectamos la marca en el frame capturado (return -1 si error)
  if(arDetectMarker(dataPtr, 125, &marker_info, &marker_num) < 0) {
    cleanup(); exit(0);   // Si devolvio -1, salimos del programa!
  }

  arVideoCapNext();

  free(instances);
  instances = NULL;
  ninstances = 0;
  for (i=0; i<marker_num; i++) {
    if (marker_info[i].cf < CF_THRESHOLD) {
      continue;
    }

    for (j=0; j<nclasses; j++) {
      if(marker_info[i].id == classes[j].id) {
        ninstances++;
        instances = (struct TObjectInstance *)
          realloc(instances, sizeof(struct TObjectInstance) * ninstances);
        instances[ninstances-1].class = &classes[j];
        arGetTransMat(&marker_info[i], classes[j].center, classes[j].width, instances[ninstances-1].patt_trans);
        break;
      }
    }
  }

  if(arMultiGetTransMat(marker_info, marker_num, mMarker) > 0) {
    updateState();
    draw();
  }
  
  argSwapBuffers();
}

static void initAudio() {
  sem_init(&audioSemaphore, 0, 1);
  ao_initialize();
  int default_driver = ao_default_driver_id();

  ao_sample_format format;
  format.bits = 16;
  format.channels = 2;
  format.rate = 44100;
  format.byte_format = AO_FMT_NATIVE;
  format.matrix = 0;

  device = ao_open_live(default_driver, &format, NULL);
  if (device == NULL) {
      print_error("Error opening device.");
  }

  pthread_create(&audioThread, NULL, *play, NULL);
}

// ======== Main ====================================================
int main(int argc, char **argv) {
  glutInit(&argc, argv);
  init();
  initAudio();
  
  arVideoCapStart();
  argMainLoop( NULL, keyboard, mainLoop );
  cleanup();
  return (0);
}
