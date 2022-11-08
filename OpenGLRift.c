#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <stdbool.h>
#ifdef USEGLEW
#include <GL/glew.h>
#endif
//  OpenGL with prototypes for glext
#define GL_GLEXT_PROTOTYPES
#ifdef __APPLE__
#include <GLUT/glut.h>
// Tell Xcode IDE to not gripe about OpenGL deprecation
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#include <GL/glut.h>
#endif
//  Default resolution
//  For Retina displays compile with -DRES=2
#ifndef RES
#define RES 1
#endif

int th=0, ph=0, fov=55, height=9*50, width=16*50, 
lightOn=1, ambient=30, diffuse=100, specular=0, emission=00,
distance=7, zh=90, inc=10, move=1;
double asp=1, dim=9, shiny=1, ylight=3;
unsigned int grass, rock, cobblestone;

#define Cos(th) cos(3.14159265/180*(th))
#define Sin(th) sin(3.14159265/180*(th))

#define LEN 8192

void Print(const char* format , ...)
{
   char    buf[LEN];
   char*   ch=buf;
   va_list args;
   va_start(args,format);
   vsnprintf(buf,LEN,format,args);
   va_end(args);
   while (*ch)
      glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18,*ch++);
}

void ErrCheck(const char* where)
{
   int err = glGetError();
   if (err) fprintf(stderr,"ERROR: %s [%s]\n",gluErrorString(err),where);
}

void Fatal(const char* format , ...)
{
   va_list args;
   va_start(args,format);
   vfprintf(stderr,format,args);
   va_end(args);
   exit(1);
}

static void Project(){
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(fov, asp, dim / 4, dim * 4);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void Reverse(void* x,const int n)
{
   char* ch = (char*)x;
   for (int k=0;k<n/2;k++)
   {
      char tmp = ch[k];
      ch[k] = ch[n-1-k];
      ch[n-1-k] = tmp;
   }
}

//
//  Load texture from BMP file
//
unsigned int LoadTexBMP(const char* file)
{
   //  Open file
   FILE* f = fopen(file,"rb");
   if (!f) Fatal("Cannot open file %s\n",file);
   //  Check image magic
   unsigned short magic;
   if (fread(&magic,2,1,f)!=1) Fatal("Cannot read magic from %s\n",file);
   if (magic!=0x4D42 && magic!=0x424D) Fatal("Image magic not BMP in %s\n",file);
   //  Read header
   unsigned int dx,dy,off,k; // Image dimensions, offset and compression
   unsigned short nbp,bpp;   // Planes and bits per pixel
   if (fseek(f,8,SEEK_CUR) || fread(&off,4,1,f)!=1 ||
       fseek(f,4,SEEK_CUR) || fread(&dx,4,1,f)!=1 || fread(&dy,4,1,f)!=1 ||
       fread(&nbp,2,1,f)!=1 || fread(&bpp,2,1,f)!=1 || fread(&k,4,1,f)!=1)
     Fatal("Cannot read header from %s\n",file);
   //  Reverse bytes on big endian hardware (detected by backwards magic)
   if (magic==0x424D)
   {
      Reverse(&off,4);
      Reverse(&dx,4);
      Reverse(&dy,4);
      Reverse(&nbp,2);
      Reverse(&bpp,2);
      Reverse(&k,4);
   }
   //  Check image parameters
   unsigned int max;
   glGetIntegerv(GL_MAX_TEXTURE_SIZE,(int*)&max);
   if (dx<1 || dx>max) Fatal("%s image width %d out of range 1-%d\n",file,dx,max);
   if (dy<1 || dy>max) Fatal("%s image height %d out of range 1-%d\n",file,dy,max);
   if (nbp!=1)  Fatal("%s bit planes is not 1: %d\n",file,nbp);
   if (bpp!=24) Fatal("%s bits per pixel is not 24: %d\n",file,bpp);
   if (k!=0)    Fatal("%s compressed files not supported\n",file);
#ifndef GL_VERSION_2_0
   //  OpenGL 2.0 lifts the restriction that texture size must be a power of two
   for (k=1;k<dx;k*=2);
   if (k!=dx) Fatal("%s image width not a power of two: %d\n",file,dx);
   for (k=1;k<dy;k*=2);
   if (k!=dy) Fatal("%s image height not a power of two: %d\n",file,dy);
#endif

   //  Allocate image memory
   unsigned int size = 3*dx*dy;
   unsigned char* image = (unsigned char*) malloc(size);
   if (!image) Fatal("Cannot allocate %d bytes of memory for image %s\n",size,file);
   //  Seek to and read image
   if (fseek(f,off,SEEK_SET) || fread(image,size,1,f)!=1) Fatal("Error reading data from image %s\n",file);
   fclose(f);
   //  Reverse colors (BGR -> RGB)
   for (k=0;k<size;k+=3)
   {
      unsigned char temp = image[k];
      image[k]   = image[k+2];
      image[k+2] = temp;
   }

   //  Sanity check
   ErrCheck("LoadTexBMP");
   //  Generate 2D texture
   unsigned int texture;
   glGenTextures(1,&texture);
   glBindTexture(GL_TEXTURE_2D,texture);
   //  Copy image
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,dx,dy,0,GL_RGB,GL_UNSIGNED_BYTE,image);
   if (glGetError()) Fatal("Error in glTexImage2D %s %dx%d\n",file,dx,dy);
   //  Scale linearly when image size doesn't match
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);

   //  Free image memory
   free(image);
   //  Return texture name
   return texture;
}


void KrugFoot(double x, double y, double z,double dx, double dy, double dz){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glScaled(dx,dy,dz);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D,rock);
    //top
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0,1,0);
    glTexCoord2f(.66,1); glVertex3f(0.1,0,0.3); //bottom right
    glTexCoord2f(1,.33); glVertex3f(0.17,0,0.1); //top right
    glTexCoord2f(.5,0); glVertex3f(0,0,0); //tip
    glTexCoord2f(0,.33); glVertex3f(-0.17,0,0.1); //top left
    glTexCoord2f(.33,1); glVertex3f(-0.1,0,0.3); //bottom left
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //front
    glNormal3f(0,0,1);
    glTexCoord2f(0,0); glVertex3f(-0.1,-0.6,0.3);
    glTexCoord2f(1,0); glVertex3f(-0.1,0,0.3);
    glTexCoord2f(1,1); glVertex3f(0.1,-0.6,0.3);
    glTexCoord2f(0,1); glVertex3f(0.1,0,0.3); 
    //front right
    glNormal3f(1,0,1);
    glTexCoord2f(0,0); glVertex3f(0.17,-0.6,0.1);
    glTexCoord2f(1,0); glVertex3f(0.17,0,0.1);
    //back right
    glNormal3f(0,0,-1);
    glTexCoord2f(1,1); glVertex3f(0,-0.6,0);
    glTexCoord2f(0,1); glVertex3f(0,0,0); 
    //back left
    glNormal3f(0,0,-1);
    glTexCoord2f(0,0); glVertex3f(-0.17,-0.6,0.1);
    glTexCoord2f(1,0); glVertex3f(-0.17,0,0.1); 
    //front left
    glNormal3f(-1,0,1);
    glTexCoord2f(1,1); glVertex3f(-0.1,-0.6,0.3);
    glTexCoord2f(0,1); glVertex3f(-0.1,0,0.3); 
    glEnd();

    //bottom
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0,-1,0);
    glTexCoord2f(.66,1); glVertex3f(0.1,-0.6,0.3); //bottom right
    glTexCoord2f(1,.33); glVertex3f(0.17,-0.6,0.1); //top right
    glTexCoord2f(.5,0); glVertex3f(0,-0.6,0); //tip
    glTexCoord2f(0,.33); glVertex3f(-0.17,-0.6,0.1); //top left
    glTexCoord2f(.33,1); glVertex3f(-0.1,-0.6,0.3); //bottom left
    glEnd();
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);
}

void Krug(double x, double y, double z, double dx, double dy, double dz, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glScaled(dx,dy,dz);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D,rock);
    //top
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0,1,0.2);
    glTexCoord2f(.9,1); glVertex3f(1,0,1.3); //bottom right
    glTexCoord2f(1,.45); glVertex3f(1.2,0.4,0); //top right
    glTexCoord2f(.5,0); glVertex3f(0,0.8,-1); //tip
    glTexCoord2f(0,.45); glVertex3f(-1.2,0.4,0); //top left
    glTexCoord2f(.1,1); glVertex3f(-1,0,1.3); //bottom left
    glEnd();
    glBegin(GL_QUADS);
    //front
    glNormal3f(0,0,1);
    glTexCoord2f(0,0); glVertex3f(0.8,-0.75,1.3);
    glTexCoord2f(1,0); glVertex3f(1,0,1.3);
    glTexCoord2f(1,1); glVertex3f(-1,0,1.3);
    glTexCoord2f(0,1); glVertex3f(-0.8,-0.75,1.3);
    //front right
    glNormal3f(1,0,1);
    glTexCoord2f(0,0); glVertex3f(1,-0.75,0);
    glTexCoord2f(1,0); glVertex3f(1.2,0.4,0);
    glTexCoord2f(1,1); glVertex3f(1,0,1.3);
    glTexCoord2f(0,1); glVertex3f(0.8,-0.75,1.3);
    //back right
    glNormal3f(0,0,-1);
    glTexCoord2f(0,0); glVertex3f(0,-0.75,-0.5);
    glTexCoord2f(1,0); glVertex3f(0,0.8,-1);
    glTexCoord2f(1,1); glVertex3f(1.2,0.4,0);
    glTexCoord2f(0,1); glVertex3f(1,-0.75,0);
    //back left
    glNormal3f(0,0,-1);
    glTexCoord2f(0,0); glVertex3f(0,-0.75,-0.5);
    glTexCoord2f(1,0); glVertex3f(0,0.8,-1);
    glTexCoord2f(1,1); glVertex3f(-1.2,0.4,0);
    glTexCoord2f(0,1); glVertex3f(-1,-0.75,0);
    //front left
    glNormal3f(-1,0,1);
    glTexCoord2f(0,0); glVertex3f(-1,-0.75,0);
    glTexCoord2f(1,0); glVertex3f(-1.2,0.4,0);
    glTexCoord2f(1,1); glVertex3f(-1,0,1.3);
    glTexCoord2f(0,1); glVertex3f(-0.8,-0.75,1.3);
    glEnd();
    //bottom
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0,-1,0);
    glTexCoord2f(.8,1); glVertex3f(0.8,-0.75,1.3);
    glTexCoord2f(1,.45); glVertex3f(1,-0.75,0);
    glTexCoord2f(.5,0); glVertex3f(0,-0.75,-0.5);
    glTexCoord2f(0,.45); glVertex3f(-1,-0.75,0);
    glTexCoord2f(.2,1); glVertex3f(-0.8,-0.75,1.3);
    glEnd();
    //head
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0,0.5,1);
    glTexCoord2f(.6,1); glVertex3f(0.3,-0.85,1.8); //bottom right
    glTexCoord2f(1,.5); glVertex3f(0.5,-0.25,1.6); //top right
    glTexCoord2f(.5,0); glVertex3f(0,0,1.45); //tip
    glTexCoord2f(0,.5); glVertex3f(-0.5,-0.25,1.6); //topleft
    glTexCoord2f(.3,1); glVertex3f(-0.3,-0.85,1.8); //bottom left
    glEnd();

    glBegin(GL_QUADS);
    //bottom
    glNormal3f(0,-1,0);
    glTexCoord2f(0,0); glVertex3f(0.3,-0.85,1.8);
    glTexCoord2f(1,0); glVertex3f(0.3,-0.73,1.3);
    glTexCoord2f(1,1); glVertex3f(-0.3,-0.73,1.3);
    glTexCoord2f(0,1); glVertex3f(-0.3,-0.85,1.8);
    //bottom right
    glNormal3f(1,-1,0);
    glTexCoord2f(0,0); glVertex3f(0.5,-0.25,1.6);
    glTexCoord2f(1,0); glVertex3f(0.3,-0.85,1.8);
    glTexCoord2f(1,1); glVertex3f(0.3,-0.73,1.3);
    glTexCoord2f(0,1); glVertex3f(0.5,-0.25,1.3);
    //top right
    glNormal3f(1,1,0);
    glTexCoord2f(0,0); glVertex3f(0.5,-0.25,1.6);
    glTexCoord2f(1,0); glVertex3f(0,0,1.45);
    glTexCoord2f(1,1); glVertex3f(0,0,1.3);
    glTexCoord2f(0,1); glVertex3f(0.5,-0.25,1.3);
    //top left
    glNormal3f(-1,1,0);
    glTexCoord2f(0,0); glVertex3f(-0.5,-0.25,1.6);
    glTexCoord2f(1,0); glVertex3f(0,0,1.45);
    glTexCoord2f(1,1); glVertex3f(0,0,1.3);
    glTexCoord2f(0,1); glVertex3f(-0.5,-0.25,1.3);
    //bottom left
    glNormal3f(-1,-1,0);
    glTexCoord2f(0,0); glVertex3f(-0.5,-0.25,1.6);
    glTexCoord2f(1,0); glVertex3f(-0.3,-0.85,1.8);
    glTexCoord2f(1,1); glVertex3f(-0.3,-0.73,1.3);
    glTexCoord2f(0,1); glVertex3f(-0.5,-0.25,1.3);
    glEnd();
    KrugFoot(1,-0.32,0.5,2,1,2); //front left
    KrugFoot(0.7,-0.32,-0.7,2,1,2); //back left
    KrugFoot(-0.7,-0.32,-0.7,2,1,2); //back right
    KrugFoot(-1,-0.32,0.5,2,1,2); //front right
    glPopMatrix();
}

void Square(double x, double y, double z){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D,grass);
    glBegin(GL_QUADS);
    glNormal3f(0,1,0);
    glTexCoord2f(0,0); glVertex3f(0,0,0);
    glTexCoord2f(1,0); glVertex3f(2,0,0);
    glTexCoord2f(1,1); glVertex3f(2,0,2);
    glTexCoord2f(0,1); glVertex3f(0,0,2);
    glEnd();
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);
}

void Ground(){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(1.6,0,1.3);
    glColor3f(0.3,0.3,0.3);
    glNormal3f(0,1,0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D,cobblestone);
    glBegin(GL_POLYGON);
    glTexCoord2f(0.5,0.5);
    for(int th=0; th<=360;th+=15){
        glTexCoord2f(.5*Cos(th)+0.5,.5*Sin(th)+0.5);
        glVertex3f(3.1*Cos(th),-0.92,3.1*Sin(th));
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    for(int i=0; i<=40;i+=2){
        for(int j=0; j<=20;j+=2)
            Square(i-20,-0.93,j-10);
    }
    glPopMatrix();
}

void Walls(){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    double h1 = -0.919, h2 = 1;
    double wallArr[] ={
        3.3,-0.919,1,
        5,-0.919,0.75,
        7,-0.919,0,
        14,-0.919,0.75,
        14.5,-0.919,0,
        14,-0.919,-0.75,
        7,-0.919,-2,
        5,-0.919,-4.5,
        3,-0.919,-4,
        -4.5,-0.919,-3.75,
        -5.25,-0.919,-3,
        -5.5,-0.919,-1,
        -5.25,-0.919,1.25,
        -4.5,-0.919,1.5
    };
    glPushMatrix();
    glTranslated(1.6,0,1.3);
    glColor3f(0.30, 0.27, 0.21);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D,cobblestone);
    glBegin(GL_QUAD_STRIP);
    int i = 0;
    glTexCoord2f(0,0);
    for(int th=155; th<=365;th+=15){
        glNormal3f(-3.1*Cos(th),0,-3.1*Sin(th));
        glTexCoord2f(1.5*Cos(th)+.5,0); glVertex3f(3.1*Cos(th),h1,3.1*Sin(th));
        glTexCoord2f(1.5*Cos(th)+.5,1); glVertex3f(3.1*Cos(th),h2,3.1*Sin(th));
        i++;
    }
    glTexCoord2f(0,1);
    for(int i=0; i<42;i+=3){
        if(i<39) glNormal3f(-(wallArr[i+5]-wallArr[i+2]),0,(wallArr[i+3]-wallArr[i])); 
        glTexCoord2f(.5*wallArr[i],0); glVertex3f(wallArr[i],h1,wallArr[i+2]);
        glTexCoord2f(.5*wallArr[i],1); glVertex3f(wallArr[i],h2,wallArr[i+2]);
    }
    glTexCoord2f(0,0); glVertex3f(3.1*Cos(155),h1,3.1*Sin(155));
    glTexCoord2f(0,1); glVertex3f(3.1*Cos(155),h2,3.1*Sin(155));
    glEnd();

    glColor3f(0.25,0.22,0.16); //top
    glNormal3f(0,1,0);
    glBegin(GL_POLYGON);
    glTexCoord2f(.5*7,0); glVertex3f(7,h2,0);
    glTexCoord2f(.5*14,.5*.75); glVertex3f(14,h2,0.75);
    glTexCoord2f(.5*14.5,0); glVertex3f(14.5,h2,0);
    glTexCoord2f(.5*14,.5*-.75); glVertex3f(14,h2,-0.75);
    glTexCoord2f(.5*7,.5*-2); glVertex3f(7,h2,-2);
    glEnd();
    glBegin(GL_QUAD_STRIP);
    glTexCoord2f(.5*7,0); glVertex3f(7,h2,0);
    glTexCoord2f(.5*7,.5*-2); glVertex3f(7,h2,-2);
    glTexCoord2f(.5*5,.5*.75); glVertex3f(5,h2,0.75);
    glTexCoord2f(.5*5,.5*-4.5); glVertex3f(5,h2,-4.5);
    glTexCoord2f(.5*3.3,.5*1); glVertex3f(3.3,h2,1);
    glTexCoord2f(.5*3,.5*-4); glVertex3f(3,h2,-4);
    glTexCoord2f(.5*3.1*Cos(365),.5*3.1*Sin(365)); glVertex3f(3.1*Cos(365),h2,3.1*Sin(365));
    glTexCoord2f(.5*3.1*Cos(365),.5*3.1*Sin(365)); glVertex3f(3.1*Cos(365),h2,3.1*Sin(365));
    glEnd();
    glBegin(GL_QUAD_STRIP);
    glTexCoord2f(.5*-4.5,.5*1.5); glVertex3f(-4.5,h2,1.5);
    glTexCoord2f(.5*-5.25,.5*1.25); glVertex3f(-5.25,h2,1.25);
    glTexCoord2f(.5*3.1*Cos(155),.5*3.1*Sin(155)); glVertex3f(3.1*Cos(155),h2,3.1*Sin(155));
    glTexCoord2f(.5*-5.5,.5*-1); glVertex3f(-5.5,h2,-1);
    glTexCoord2f(.5*3.1*Cos(170),.5*3.1*Sin(170)); glVertex3f(3.1*Cos(170),h2,3.1*Sin(170));
    glTexCoord2f(.5*-5.5,.5*-1); glVertex3f(-5.5,h2,-1);
    glTexCoord2f(.5*3.1*Cos(185),.5*3.1*Sin(185)); glVertex3f(3.1*Cos(185),h2,3.1*Sin(185));
    glTexCoord2f(.5*-5.5,.5*-1); glVertex3f(-5.5,h2,-1);
    glTexCoord2f(.5*3.1*Cos(200),.5*3.1*Sin(200)); glVertex3f(3.1*Cos(200),h2,3.1*Sin(200));
    glTexCoord2f(.5*-5.25,.5*-3); glVertex3f(-5.25,h2,-3);
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(.5*3,.5*-4); glVertex3f(3,h2,-4);
    glTexCoord2f(.5*3.1*Cos(365),.5*3.1*Sin(365)); glVertex3f(3.1*Cos(365),h2,3.1*Sin(365));
    glTexCoord2f(.5*3.1*Cos(350),.5*3.1*Sin(350)); glVertex3f(3.1*Cos(350),h2,3.1*Sin(350));
    glTexCoord2f(.5*3.1*Cos(335),.5*3.1*Sin(335)); glVertex3f(3.1*Cos(335),h2,3.1*Sin(335));
    glTexCoord2f(.5*3.1*Cos(320),.5*3.1*Sin(320)); glVertex3f(3.1*Cos(320),h2,3.1*Sin(320));
    glTexCoord2f(.5*3.1*Cos(305),.5*3.1*Sin(305)); glVertex3f(3.1*Cos(305),h2,3.1*Sin(305));
    glTexCoord2f(.5*3.1*Cos(290),.5*3.1*Sin(290)); glVertex3f(3.1*Cos(290),h2,3.1*Sin(290));
    glTexCoord2f(.5*3.1*Cos(275),.5*3.1*Sin(275)); glVertex3f(3.1*Cos(275),h2,3.1*Sin(275));
    glTexCoord2f(.5*3.1*Cos(260),.5*3.1*Sin(260)); glVertex3f(3.1*Cos(260),h2,3.1*Sin(260));
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(.5*-4.5,.5*-3); glVertex3f(-4.5,h2,-3.75);
    glTexCoord2f(.5*-5.25,.5*-3); glVertex3f(-5.25,h2,-3);
    glTexCoord2f(.5*3.1*Cos(200),.5*3.1*Sin(200)); glVertex3f(3.1*Cos(200),h2,3.1*Sin(200));
    glTexCoord2f(.5*3.1*Cos(215),.5*3.1*Sin(215)); glVertex3f(3.1*Cos(215),h2,3.1*Sin(215));
    glTexCoord2f(.5*3.1*Cos(230),.5*3.1*Sin(230)); glVertex3f(3.1*Cos(230),h2,3.1*Sin(230));
    glTexCoord2f(.5*3.1*Cos(245),.5*3.1*Sin(245)); glVertex3f(3.1*Cos(245),h2,3.1*Sin(245));
    glTexCoord2f(.5*3.1*Cos(260),.5*3.1*Sin(260)); glVertex3f(3.1*Cos(260),h2,3.1*Sin(260));
    glTexCoord2f(.5*3,.5*-4); glVertex3f(3,h2,-4);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
}

void ball(double x,double y,double z,double r){
   //  Save transformation
   glPushMatrix();
   //  Offset, scale and rotate
   glTranslated(x,y,z);
   glScaled(r,r,r);
   //  White ball with yellow specular
   float yellow[]   = {1.0,1.0,0.0,1.0};
   float Emission[] = {0.0,0.0,0.01*emission,1.0};
   glColor3f(1,1,1);
   glMaterialf(GL_FRONT,GL_SHININESS,shiny);
   glMaterialfv(GL_FRONT,GL_SPECULAR,yellow);
   glMaterialfv(GL_FRONT,GL_EMISSION,Emission);
   //  Bands of latitude
   for (int ph=-90;ph<90;ph+=inc)
   {
      glBegin(GL_QUAD_STRIP);
      for (int th=0;th<=360;th+=2*inc)
      {
        glVertex3f(Sin(th)*Cos(ph), Cos(th)*Cos(ph),Sin(ph));
        glNormal3d(Sin(th)*Cos(ph), Cos(th)*Cos(ph),Sin(ph));
        glVertex3f(Sin(th)*Cos(ph+inc), Cos(th)*Cos(ph+inc),Sin(ph+inc));
        glNormal3d(Sin(th)*Cos(ph+inc), Cos(th)*Cos(ph+inc),Sin(ph+inc));
      }
      glEnd();
   }
   glPopMatrix();
}

void display()
{
    //glClearColor(0.31, 0.43, 0.08, 1.0);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glLoadIdentity();
    double Ex = -2*dim*Sin(th)*Cos(ph);
    double Ey = +2*dim        *Sin(ph);
    double Ez = +2*dim*Cos(th)*Cos(ph);
    gluLookAt(Ex,Ey,Ez , 0,0,0 , 0,Cos(ph),0);
    glShadeModel(GL_SMOOTH);
    if(lightOn){
        float Ambient[] = {0.01*ambient ,0.01*ambient ,0.01*ambient ,1.0};
        float Diffuse[] = {0.01*diffuse ,0.01*diffuse ,0.01*diffuse ,1.0};
        float Specular[] = {0.01*specular,0.01*specular,0.01*specular,1.0};

        float Position[]  = {distance*Cos(zh),ylight,distance*Sin(zh),1.0};
        glColor3f(1,1,1);
        ball(Position[0],Position[1],Position[2] , 0.1);
        glEnable(GL_NORMALIZE);
        glEnable(GL_LIGHTING);
        glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_COLOR_MATERIAL);
        glEnable(GL_LIGHT0);
        glLightfv(GL_LIGHT0,GL_AMBIENT ,Ambient);
        glLightfv(GL_LIGHT0,GL_DIFFUSE ,Diffuse);
        glLightfv(GL_LIGHT0,GL_SPECULAR,Specular);
        glLightfv(GL_LIGHT0,GL_POSITION,Position);
    }
    else glDisable(GL_LIGHTING);
    Ground();
    Krug(3,0,0,1,1,1,-5); //main big krug
    Krug(0,-0.225,0,0.75,0.75,0.75,10); //medium krug
    Krug(-0.5,-0.55,2.5,0.4,0.4,0.4,45); //small krugs
    Krug(1.5,-0.55,1.75,0.4,0.4,0.4,5);
    Walls();
    glColor3f(1,1,1);
    glWindowPos2i(5,5);
    Print("Angle=%d,%d Dim=%.1f FOV=%d Light Height=%.1f",th,ph,dim,fov,ylight);
    ErrCheck("display");
    glFlush();
    glutSwapBuffers();
}

void idle()
{
   double t = glutGet(GLUT_ELAPSED_TIME)/1000.0;
   zh = fmod(90*t,360.0);
   glutPostRedisplay();
}

void special(int key,int x,int y)
{
   if (key == GLUT_KEY_RIGHT)
      th += 5;
   else if (key == GLUT_KEY_LEFT)
      th -= 5;
   else if (key == GLUT_KEY_UP)
      ph += 5;
   else if (key == GLUT_KEY_DOWN)
      ph -= 5;
   th %= 360;
   ph %= 360;
   Project();
   glutPostRedisplay();
}

void key(unsigned char ch,int x,int y)
{
    if (ch == 27)
        exit(0);
    else if (ch == '-' && ch>1) //fov down
        fov--;
    else if (ch == '=' && ch<179) //fov up
        fov++;
    else if (ch == '[') //dim up
        dim += 0.1;
    else if (ch == ']' && dim>1) //dim down
        dim -= 0.1;
    else if(ch == 'l' || ch == 'L') //light on/off
        lightOn = 1-lightOn;
    else if (ch == 's' || ch == 'S') //stop/start light movement
        move = 1-move;
    else if (ch == '.') //move light manually
        zh += 1;
    else if (ch == ',')
        zh -= 1;
    else if (ch == '9') //light height
        ylight -= 0.1;
    else if (ch == '0')
        ylight += 0.1;
    
    glutIdleFunc(move?idle:NULL);
    Project();
    glutPostRedisplay();
}

void reshape(int width,int height)
{
    asp = (height>0) ? (double)width/height : 1;
    //  Set the viewport to the entire window
    glViewport(0,0, RES*width,RES*height);
    //  Set projection
    Project();
}

int main(int argc,char* argv[])
{
    glutInit(&argc,argv);
    glutInitWindowSize(width,height);
    glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
    glutCreateWindow("OpenGL Rift");
#ifdef USEGLEW
    if (glewInit()!=GLEW_OK) Fatal("Error initializing GLEW\n");
#endif
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutSpecialFunc(special);
    glutKeyboardFunc(key);
    glutIdleFunc(idle);
    grass = LoadTexBMP("grass.bmp");
    rock = LoadTexBMP("stone.bmp");
    cobblestone = LoadTexBMP("cobblestone.bmp");
    glutMainLoop();
    return 0;
}
