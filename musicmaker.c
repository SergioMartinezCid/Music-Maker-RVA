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
#define STEP_FREQUENCY 500

// ==== Definicion de constantes y variables globales ===============
ARMultiMarkerInfoT  *mMarker;       // Estructura global Multimarca
int stepIndex = 0;
long long lastTimeChangedStep;

void print_error (char *error) {  printf("%s\n",error); exit(0); }
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
  
  glMaterialfv(GL_FRONT, GL_AMBIENT, barMaterial);
  glTranslatef(BAR_WIDTH*(stepIndex + 0.5), -1*PAPER_HALF_HEIGHT, 1.0);
  glScalef(BAR_WIDTH, PAPER_HEIGHT, 15);
  glutSolidCube(1);    

  glDisable(GL_DEPTH_TEST);
}

// ======== init ====================================================
static void init( void ) {
  ARParam  wparam, cparam;   // Parametros intrinsecos de la camara
  int xsize, ysize;          // Tamano del video de camara (pixels)
  
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

  lastTimeChangedStep = timeInMilliseconds();
  argInit(&cparam, 1.0, 0, 0, 0, 0);   // Abrimos la ventana 
}

// ======== mainLoop ================================================
static void mainLoop(void) {
  ARUint8 *dataPtr;
  ARMarkerInfo *marker_info;
  int marker_num;

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
