#include <GL/glut.h>    
#include <AR/gsub.h>    
#include <AR/video.h>   
#include <AR/param.h>   
#include <AR/ar.h>
#include <AR/arMulti.h>
#include <sys/time.h>
#include <time.h>

#define PAPER_WIDTH 594.0
#define PAPER_HEIGHT 420.0
#define PAPER_HALF_WIDTH PAPER_WIDTH / 2.0
#define PAPER_HALF_HEIGHT PAPER_HEIGHT / 2.0
#define STEPS_NUMBER 8
#define BAR_WIDTH PAPER_WIDTH / STEPS_NUMBER
#define STEP_FREQUENCY 1000 /* 500 TODO CHANGE BACK */
#define CF_THRESHOLD 0.8

enum Class {
  NOTE,
  PIANO,
  XILOPHONE,
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

struct TObjectClass *classes = NULL;
int nclasses = 0;
struct TObjectInstance *instances = NULL;
int ninstances = 0;

ARMultiMarkerInfoT  *mMarker;
int stepIndex = 0;
long long lastTimeChangedStep;

void print_error (char *error) {  printf("%s\n",error); exit(0); }
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
  arVideoCapStop();   arVideoClose();   argCleanup();   exit(0);
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

    // Check for all entities in the new bar position
    double xMMark = mMarker->trans[0][3],
           yMMark = mMarker->trans[1][3];
    for (int i=0; i<ninstances; i++) {
      // TODO: avoid using the switch statement and use the TObjectClass
      switch (instances[i].class->classCode) {
        case NOTE:
        case PIANO:
        case XILOPHONE:
          double x = instances[i].patt_trans[0][3] - xMMark,
                 y = instances[i].patt_trans[1][3] - yMMark;
          if (stepIndex*BAR_WIDTH < x && x < (stepIndex+1)*BAR_WIDTH && 0 < y && y < PAPER_HEIGHT) {
            printf("MATCH\n");
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
  int i;
  
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
  glPushMatrix();
  glMaterialfv(GL_FRONT, GL_AMBIENT, barMaterial);
  glTranslatef(BAR_WIDTH*(stepIndex + 0.5), -1*PAPER_HALF_HEIGHT, 1.0);
  glScalef(BAR_WIDTH, PAPER_HEIGHT, 15);
  glutSolidCube(1);    
  glPopMatrix();

  for (i=0; i<ninstances; i++) {
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
  double c[2] = {0.0, 0.0};  // Centro de patron (por defecto)
  
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

  addClass(NOTE, "data/note.patt",           50, c, drawNote); 
  addClass(PIANO, "data/piano.patt",         50, c, drawNote); 
  addClass(XILOPHONE, "data/xilophone.patt", 50, c, drawNote); 
  addClass(METRONOME, "data/metronome.patt", 50, c, drawNote); 

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

// ======== Main ====================================================
int main(int argc, char **argv) {
  glutInit(&argc, argv);
  init();
  
  arVideoCapStart();
  argMainLoop( NULL, keyboard, mainLoop );
  return (0);
}
