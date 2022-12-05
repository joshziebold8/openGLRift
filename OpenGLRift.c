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

int th=0, ph=0, fov=55, height=15*50, width=15*50, 
lightOn=1, ambient=30, diffuse=100, specular=0, emission=00,
distance=10, zh=90, inc=10, move=1, mode=22;
double asp=1, dim=10, shiny=1, ylight=3, eyeX, eyeZ;
unsigned int grass, rock, cobblestone, map;

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

void Square(double x, double y, double z){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    // glEnable(GL_TEXTURE_2D);
    // glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    // glBindTexture(GL_TEXTURE_2D,grass);
    glColor3f(0.21, 0.39, 0.12);
    glBegin(GL_QUADS);
    glNormal3f(0,1,0);
    glTexCoord2f(0,0); glVertex3f(0,0,0);
    glTexCoord2f(1,0); glVertex3f(2,0,0);
    glTexCoord2f(1,1); glVertex3f(2,0,2);
    glTexCoord2f(0,1); glVertex3f(0,0,2);
    glEnd();
    glPopMatrix();
    // glDisable(GL_TEXTURE_2D);
}

void betterGround(){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    
    glColor3f(1,1,1);
    glBegin(GL_LINE_LOOP);
    glVertex3f(0,0,0);
    glVertex3f(30,0,0);
    glVertex3f(30,0,-30);
    glVertex3f(0,0,-30);
    glEnd();
    for(int i=0; i<30;i+=2){
        for(int j=2; j<=30;j+=2)
            Square(i,0,-j);
    }
}

void baseWalls(double x, double y, double z, double th, double ph){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glRotated(ph,1,0,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(1.5,0,0);
    glVertex3f(1.5,0,0.4);
    glVertex3f(0,0,0.4);
    glEnd();

    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1.5,0.8,0);
    glVertex3f(1.5,0.8,0.4);
    glVertex3f(0,0.8,0.4);
    glEnd();

    //sides
    glBegin(GL_QUAD_STRIP);
    //back
    glNormal3f(0,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1.5,0,0);
    glVertex3f(1.5,0.8,0);
    //right
    glNormal3f(1,0,0);
    glVertex3f(1.5,0,0.4);
    glVertex3f(1.5,0.8,0.4);
    //front
    glNormal3f(0,0,1);
    glVertex3f(0,0,0.4);
    glVertex3f(0,0.8,0.4);
    //left
    glNormal3f(1,0,0);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();


    glPopMatrix();
}

void red1(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(2,0,0);
    glVertex3f(2.5,0,0.5);
    glVertex3f(0.5,0,1.7);
    glVertex3f(0,0,1.5);
    glEnd();

    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(2,0.8,0);
    glVertex3f(2.5,0.8,0.5);
    glVertex3f(0.5,0.8,1.7);
    glVertex3f(0,0.8,1.5);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //back
    glNormal3f(0,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(2,0,0);
    glVertex3f(2,0.8,0);
    //backright
    glNormal3f(1,0,-1);
    glVertex3f(2.5,0,0.5);
    glVertex3f(2.5,0.8,0.5);
    //frontright
    glNormal3f(0.5,0,1);
    glVertex3f(0.5,0,1.7);
    glVertex3f(0.5,0.8,1.7);
    //frontleft
    glNormal3f(-1,0,1);
    glVertex3f(0,0,1.5);
    glVertex3f(0,0.8,1.5);
    //left
    glNormal3f(-1,0,0);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);

    glEnd();
    glPopMatrix();
}

void red2(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(0.75,0,1.25);
    glVertex3f(0,0,1.5);
    glVertex3f(0,0,3);
    glVertex3f(-0.8,0,3);
    glVertex3f(-0.8,0,0.3);
    glEnd();

    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.75,0.8,1.25);
    glVertex3f(0,0.8,1.5);
    glVertex3f(0,0.8,3);
    glVertex3f(-0.8,0.8,3);
    glVertex3f(-0.8,0.8,0.3);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //backright
    glNormal3f(1,0,-0.5);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.75,0,1.25);
    glVertex3f(0.75,0.8,1.25);
    //frontright
    glNormal3f(0.1,0,1);
    glVertex3f(0,0,1.5);
    glVertex3f(0,0.8,1.5);
    //right
    glNormal3f(1,0,0);
    glVertex3f(0,0,3);
    glVertex3f(0,0.8,3);
    //front
    glNormal3f(0,0,1);
    glVertex3f(-0.8,0,3);
    glVertex3f(-0.8,0.8,3);
    //left
    glNormal3f(-1,0,0);
    glVertex3f(-0.8,0,0.3);
    glVertex3f(-0.8,0.8,0.3);
    //backleft
    glNormal3f(-0.5,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void red3(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottomback
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(2.5,0,0);
    glVertex3f(2.5,0,0.8);
    glVertex3f(0.8,0,0.5);
    glEnd();
    //bottomleft
    glBegin(GL_POLYGON);
    glVertex3f(0.8,0,0.5);
    glVertex3f(0,0,2);
    glVertex3f(0.3,0,2.2);
    glVertex3f(1.3,0,0.5);
    glEnd();
    //bottomright
    glBegin(GL_POLYGON);
    glVertex3f(2.5,0,0.8);
    glVertex3f(2.5,0,2.7);
    glVertex3f(2.3,0,2.75);
    glVertex3f(2,0,2.4);
    glVertex3f(2.1,0,2);
    glVertex3f(1.9,0,0.5);
    glEnd();

    //topback
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(2.5,0.8,0);
    glVertex3f(2.5,0.8,0.8);
    glVertex3f(0.8,0.8,0.5);
    glEnd();
    //topleft
    glBegin(GL_POLYGON);
    glVertex3f(0.8,0.8,0.5);
    glVertex3f(0,0.8,2);
    glVertex3f(0.3,0.8,2.2);
    glVertex3f(1.3,0.8,0.5);
    glEnd();
    //topright
    glBegin(GL_POLYGON);
    glVertex3f(2.5,0.8,0.8);
    glVertex3f(2.5,0.8,2.7);
    glVertex3f(2.3,0.8,2.75);
    glVertex3f(2,0.8,2.4);
    glVertex3f(2.1,0.8,2);
    glVertex3f(1.9,0.8,0.5);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //back
    glNormal3f(0,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(2.5,0,0);
    glVertex3f(2.5,0.8,0);
    //right
    glNormal3f(1,0,0);
    glVertex3f(2.5,0,2.7);
    glVertex3f(2.5,0.8,2.7);
    //frontrightright
    glNormal3f(0.1,0,1);
    glVertex3f(2.3,0,2.75);
    glVertex3f(2.3,0.8,2.75);
    //frontrightleft
    glNormal3f(-1,0,1);
    glVertex3f(2,0,2.4);
    glVertex3f(2,0.8,2.4);
    //infrontright
    glNormal3f(-1,0,-0.5);
    glVertex3f(2.1,0,2);
    glVertex3f(2.1,0.8,2);
    //inright
    glNormal3f(-1,0,0.1);
    glVertex3f(1.925,0,0.6845);
    glVertex3f(1.925,0.8,0.6845);
    //inback
    glNormal3f(0,0,1);
    glVertex3f(1.2605,0,0.5722);
    glVertex3f(1.2605,0.8,0.5722);
    //inleft
    glNormal3f(1,0,0.5);
    glVertex3f(0.3,0,2.2);
    glVertex3f(0.3,0.8,2.2);
    //front left
    glNormal3f(-1,0,1);
    glVertex3f(0,0,2);
    glVertex3f(0,0.8,2);
    //leftfront
    glNormal3f(-1,0,-0.5);
    glVertex3f(0.8,0,0.5);
    glVertex3f(0.8,0.8,0.5);
    //leftback
    glNormal3f(-0.4,0,1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();

    glPopMatrix();
}

void red4(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottomtop
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(0.6,0,0);
    glVertex3f(1.2,0,-1);
    glVertex3f(1,0,-1.2);
    glVertex3f(0.4,0,-1.2);
    glVertex3f(0.2,0,-0.8);
    glEnd();
    //bottombottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(0.2,0,1.4);
    glVertex3f(0.6,0,1.6);
    glVertex3f(1,0,1.4);
    glVertex3f(0.6,0,0);
    glEnd();

    //toptop
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.6,0.8,0);
    glVertex3f(1.2,0.8,-1);
    glVertex3f(1,0.8,-1.2);
    glVertex3f(0.4,0.8,-1.2);
    glVertex3f(0.2,0.8,-0.8);
    glEnd();
    //topbottom
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.2,0.8,1.4);
    glVertex3f(0.6,0.8,1.6);
    glVertex3f(1,0.8,1.4);
    glVertex3f(0.6,0.8,0);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //leftmid
    glNormal3f(-1,0,-0.1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.2,0,-0.8);
    glVertex3f(0.2,0.8,-0.8);
    //lefttop
    glNormal3f(-1,0,-0.2);
    glVertex3f(0.4,0,-1.2);
    glVertex3f(0.4,0.8,-1.2);
    //top
    glNormal3f(0,0,-1);
    glVertex3f(1,0,-1.2);
    glVertex3f(1,0.8,-1.2);
    //topright
    glNormal3f(0.9,0,-1);
    glVertex3f(1.2,0,-1);
    glVertex3f(1.2,0.8,-1);
    //righttop
    glNormal3f(1,0,0.6);
    glVertex3f(0.6,0,0);
    glVertex3f(0.6,0.8,0);
    //rightbot
    glNormal3f(1,0,-0.2);
    glVertex3f(1,0,1.4);
    glVertex3f(1,0.8,1.4);
    //bottomright
    glNormal3f(0.2,0,1);
    glVertex3f(0.6,0,1.6);
    glVertex3f(0.6,0.8,1.6);
    //bottomleft
    glNormal3f(-0.2,0,1);
    glVertex3f(0.2,0,1.4);
    glVertex3f(0.2,0.8,1.4);
    //rightbottom
    glNormal3f(-1,0,0);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void red5(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(1.6,0,-0.1);
    glVertex3f(3.2,0,1);
    glVertex3f(3.2,0,1.2);
    glVertex3f(0.2,0,1.2);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1.6,0.8,-0.1);
    glVertex3f(3.2,0.8,1);
    glVertex3f(3.2,0.8,1.2);
    glVertex3f(0.2,0.8,1.2);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //back
    glNormal3f(0,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1.6,0,-0.1);
    glVertex3f(1.6,0.8,-0.1);
    //backright
    glNormal3f(0.9,0,-1);
    glVertex3f(3.2,0,1);
    glVertex3f(3.2,0.8,1);
    //right
    glNormal3f(1,0,0);
    glVertex3f(3.2,0,1.2);
    glVertex3f(3.2,0.8,1.2);
    //front
    glNormal3f(0,0,1);
    glVertex3f(0.2,0,1.2);
    glVertex3f(0.2,0.8,1.2);
    //left
    glNormal3f(-1,0,0.02);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void red6(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottomleft
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(1.3,0,-0.3);
    glVertex3f(1.9,0,-0.3);
    glVertex3f(0.4,0,1);
    glVertex3f(0,0,0.6);
    glEnd();
    //bottommidleft
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(1.9,0,-0.3);
    glVertex3f(2.9,0,-0.3);
    glVertex3f(3.3,0,0.5);
    glVertex3f(0.85,0,0.4);
    glEnd();
    //bottommidright
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(3.3,0,0.5);
    glVertex3f(3,0,1.4);
    glVertex3f(2.2,0,1.2);
    glVertex3f(2,0,0.43);
    glEnd();
    //bottomright
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(3,0,1.4);
    glVertex3f(2.5,0,2);
    glVertex3f(1.8,0,2);
    glVertex3f(1.7,0,1.8);
    glVertex3f(2.2,0,1.2);
    glEnd();
    //topleft
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1.3,0.8,-0.3);
    glVertex3f(1.9,0.8,-0.3);
    glVertex3f(0.4,0.8,1);
    glVertex3f(0,0.8,0.6);
    glEnd();
    //topmidleft
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(1.9,0.8,-0.3);
    glVertex3f(2.9,0.8,-0.3);
    glVertex3f(3.3,0.8,0.5);
    glVertex3f(0.85,0.8,0.4);
    glEnd();
    //topmidright
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(3.3,0.8,0.5);
    glVertex3f(3,0.8,1.4);
    glVertex3f(2.2,0.8,1.2);
    glVertex3f(2,0.8,0.43);
    glEnd();
    //topright
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(3,0.8,1.4);
    glVertex3f(2.5,0.8,2);
    glVertex3f(1.8,0.8,2);
    glVertex3f(1.7,0.8,1.8);
    glVertex3f(2.2,0.8,1.2);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //backleft
    glNormal3f(-0.1,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1.3,0,-0.3);
    glVertex3f(1.3,0.8,-0.3);
    //back
    glNormal3f(0,0,-1);
    glVertex3f(2.9,0,-0.3);
    glVertex3f(2.9,0.8,-0.3);
    //backright
    glNormal3f(1,0,-0.9);
    glVertex3f(3.3,0,0.5);
    glVertex3f(3.3,0.8,0.5);
    //right
    glNormal3f(1,0,0.2);
    glVertex3f(3,0,1.4);
    glVertex3f(3,0.8,1.4);
    //rightfront
    glNormal3f(1,0,1);
    glVertex3f(2.5,0,2);
    glVertex3f(2.5,0.8,2);
    //front
    glNormal3f(0,0,1);
    glVertex3f(1.8,0,2);
    glVertex3f(1.8,0.8,2);
    //frontleft
    glNormal3f(-1,0,0.8);
    glVertex3f(1.7,0,1.8);
    glVertex3f(1.7,0.8,1.8);
    //inrightfront
    glNormal3f(-1,0,-1);
    glVertex3f(2.2,0,1.2);
    glVertex3f(2.2,0.8,1.2);
    //inrightback
    glNormal3f(-1,0,0.1);
    glVertex3f(2,0,0.44);
    glVertex3f(2,0.8,0.44);
    //inback
    glNormal3f(0,0,1);
    glVertex3f(1.09,0,0.4);
    glVertex3f(1.09,0.8,0.4);
    //inleft
    glNormal3f(1,0,1);
    glVertex3f(0.4,0,1);
    glVertex3f(0.4,0.8,1);
    //leftfront
    glNormal3f(-1,0,1);
    glVertex3f(0,0,0.6);
    glVertex3f(0,0.8,0.6);
    //left
    glNormal3f(-1,0,0);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void red7(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottomright
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(2.2,0,-0.1);
    glVertex3f(2.7,0,-0.4);
    glVertex3f(3.4,0,0.05);
    glVertex3f(4.4,0,0.1);
    glVertex3f(5.4,0,0.4);
    glVertex3f(5.45,0,0.6);
    glVertex3f(5.4,0,0.7);
    glVertex3f(3.4,0,0.5);
    glVertex3f(2.4,0,1.1);
    glVertex3f(2.1,0,1.1);
    glVertex3f(2.05,0,0.7);
    glVertex3f(1.8,0,0.4);
    glVertex3f(1.15,0,0.45);
    glEnd();
    //bottomleft
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(1,0,0.05);
    glVertex3f(1.15,0,0.45);
    glVertex3f(0.8,0,0.7);
    glVertex3f(0.85,0,1.1);
    glVertex3f(0.8,0,1.1);
    glVertex3f(0.4,0,1.25);
    glVertex3f(0.1,0,1.25);
    glVertex3f(-0.15,0,1);
    glVertex3f(-0.2,0,0.3);
    glEnd();
    //bottommiddle
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(1,0,0.05);
    glVertex3f(2.2,0,-0.1);
    glVertex3f(1.15,0,0.45);
    glEnd();

    //topright
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(2.2,0.8,-0.1);
    glVertex3f(2.7,0.8,-0.4);
    glVertex3f(3.4,0.8,0.05);
    glVertex3f(4.4,0.8,0.1);
    glVertex3f(5.4,0.8,0.4);
    glVertex3f(5.45,0.8,0.6);
    glVertex3f(5.4,0.8,0.7);
    glVertex3f(3.4,0.8,0.5);
    glVertex3f(2.4,0.8,1.1);
    glVertex3f(2.1,0.8,1.1);
    glVertex3f(2.05,0.8,0.7);
    glVertex3f(1.8,0.8,0.4);
    glVertex3f(1.15,0.8,0.45);
    glEnd();
    //topleft
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1,0.8,0.05);
    glVertex3f(1.15,0.8,0.45);
    glVertex3f(0.8,0.8,0.7);
    glVertex3f(0.85,0.8,1.1);
    glVertex3f(0.8,0.8,1.1);
    glVertex3f(0.4,0.8,1.25);
    glVertex3f(0.1,0.8,1.25);
    glVertex3f(-0.15,0.8,1);
    glVertex3f(-0.2,0.8,0.3);
    glEnd();
    //topmiddle
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(1,0.8,0.05);
    glVertex3f(2.2,0.8,-0.1);
    glVertex3f(1.15,0.8,0.45);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //backleft
    glNormal3f(0.1,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1,0,0.05);
    glVertex3f(1,0.8,0.05);

    glNormal3f(-0.1,0,-1);
    glVertex3f(2.2,0,-0.1);
    glVertex3f(2.2,0.8,-0.1);

    glNormal3f(-0.7,0,-1);
    glVertex3f(2.7,0,-0.4);
    glVertex3f(2.7,0.8,-0.4);
    //backright
    glNormal3f(1,0,-0.9);
    glVertex3f(3.4,0,0.05);
    glVertex3f(3.4,0.8,0.05);

    glNormal3f(0.05,0,-1);
    glVertex3f(4.4,0,0.1);
    glVertex3f(4.4,0.8,0.1);

    glNormal3f(0.1,0,-1);
    glVertex3f(5.4,0,0.4);
    glVertex3f(5.4,0.8,0.4);
    //right
    glNormal3f(1,0,-0.05);
    glVertex3f(5.45,0,0.6);
    glVertex3f(5.45,0.8,0.6);

    glNormal3f(1,0,-0.075);
    glVertex3f(5.4,0,0.7);
    glVertex3f(5.4,0.8,0.7);
    //frontright
    glNormal3f(-0.05,0,1);
    glVertex3f(3.4,0,0.5);
    glVertex3f(3.4,0.8,0.5);

    glNormal3f(0.8,0,1);
    glVertex3f(2.4,0,1.1);
    glVertex3f(2.4,0.8,1.1);

    glNormal3f(0,0,1);
    glVertex3f(2.1,0,1.1);
    glVertex3f(2.1,0.8,1.1);
    //inright
    glNormal3f(-1,0,0.02);
    glVertex3f(2.05,0,0.7);
    glVertex3f(2.05,0.8,0.7);

    glNormal3f(-1,0,0.8);
    glVertex3f(1.8,0,0.4);
    glVertex3f(1.8,0.8,0.4);
    //inmiddle
    glNormal3f(0.01,0,1);
    glVertex3f(1.15,0,0.45);
    glVertex3f(1.15,0.8,0.45);
    //inleft
    glNormal3f(0.8,0,1);
    glVertex3f(0.8,0,0.7);
    glVertex3f(0.8,0.8,0.7);

    glNormal3f(1,0,-0.01);
    glVertex3f(0.85,0,1.1);
    glVertex3f(0.85,0.8,1.1);
    //frontleft
    glNormal3f(0,0,1);
    glVertex3f(0.8,0,1.1);
    glVertex3f(0.8,0.8,1.1);

    glNormal3f(0.2,0,1);
    glVertex3f(0.4,0,1.25);
    glVertex3f(0.4,0.8,1.25);

    glNormal3f(0,0,1);
    glVertex3f(0.1,0,1.25);
    glVertex3f(0.1,0.8,1.25);

    glNormal3f(-1,0,1);
    glVertex3f(-0.15,0,1);
    glVertex3f(-0.15,0.8,1);
    //left
    glNormal3f(-1,0,0);
    glVertex3f(-0.2,0,0.3);
    glVertex3f(-0.2,0.8,0.3);

    glNormal3f(-1,0,-0.8);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void red8(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0.2,0,0.1);
    glVertex3f(1.3,0,0);
    glVertex3f(1.8,0,0.3);
    glVertex3f(1.8,0,0.5);
    glVertex3f(0,0,0.5);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0.1);
    glVertex3f(1.3,0.8,0);
    glVertex3f(1.8,0.8,0.3);
    glVertex3f(1.8,0.8,0.5);
    glVertex3f(0,0.8,0.5);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //back
    glNormal3f(-0.05,0,-1);
    glVertex3f(0,0,0.1);
    glVertex3f(0,0.8,0.1);
    glVertex3f(1.3,0,0);
    glVertex3f(1.3,0.8,0);
    //backright
    glNormal3f(1,0,-1);
    glVertex3f(1.8,0,0.3);
    glVertex3f(1.8,0.8,0.3);
    //right
    glNormal3f(1,0,0);
    glVertex3f(1.8,0,0.5);
    glVertex3f(1.8,0.8,0.5);
    //front
    glNormal3f(0,0,1);
    glVertex3f(0,0,0.5);
    glVertex3f(0,0.8,0.5);
    //left
    glNormal3f(-1,0,0);
    glVertex3f(0,0,0.1);
    glVertex3f(0,0.8,0.1);
    glEnd();
    glPopMatrix();
}

void red9(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(0.9,0,0);
    glVertex3f(2.7,0,0.4);
    glVertex3f(2.7,0,0.8);
    glVertex3f(0.5,0,0.8);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.9,0.8,0);
    glVertex3f(2.7,0.8,0.4);
    glVertex3f(2.7,0.8,0.8);
    glVertex3f(0.5,0.8,0.8);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //backleft
    glNormal3f(0,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.9,0,0);
    glVertex3f(0.9,0.8,0);
    //backright
    glNormal3f(0.5,0,-1);
    glVertex3f(2.7,0,0.4);
    glVertex3f(2.7,0.8,0.4);
    //right
    glNormal3f(1,0,0);
    glVertex3f(2.7,0,0.8);
    glVertex3f(2.7,0.8,0.8);
    //front
    glNormal3f(0,0,1);
    glVertex3f(0.5,0,0.8);
    glVertex3f(0.5,0.8,0.8);
    //left
    glNormal3f(-1,0,0.4);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void red10(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(1,0,-0.1);
    glVertex3f(2.4,0,1);
    glVertex3f(2.4,0,1.2);
    glVertex3f(2,0,2);
    glVertex3f(1.2,0,2.3);
    glVertex3f(0,0,0.4);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1,0.8,-0.1);
    glVertex3f(2.4,0.8,1);
    glVertex3f(2.4,0.8,1.2);
    glVertex3f(2,0.8,2);
    glVertex3f(1.2,0.8,2.3);
    glVertex3f(0,0.8,0.4);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //back
    glNormal3f(-0.05,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(1,0,-0.1);
    glVertex3f(1,0.8,-0.1);
    //rightback
    glNormal3f(1,0,-1);
    glVertex3f(2.4,0,1);
    glVertex3f(2.4,0.8,1);
    //right
    glNormal3f(1,0,0);
    glVertex3f(2.4,0,1.2);
    glVertex3f(2.4,0.8,1.2);
    //rightfront
    glNormal3f(1,0,0.8);
    glVertex3f(2,0,2);
    glVertex3f(2,0.8,2);
    //frontright
    glNormal3f(0.2,0,1);
    glVertex3f(1.2,0,2.3);
    glVertex3f(1.2,0.8,2.3);
    //leftfront
    glNormal3f(-1,0,0.4);
    glVertex3f(0,0,0.4);
    glVertex3f(0,0.8,0.4);
    //left
    glNormal3f(-1,0,0);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void blue1(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,3.4);
    glVertex3f(0,0,0);
    glVertex3f(1.2,0,0.5);
    glVertex3f(1.2,0,2.85);
    glVertex3f(1.4,0,3);
    glVertex3f(1.4,0,3.2);
    glVertex3f(1.1,0,3.4);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,3.4);
    glVertex3f(0,0.8,0);
    glVertex3f(1.2,0.8,0.5);
    glVertex3f(1.2,0.8,2.85);
    glVertex3f(1.4,0.8,3);
    glVertex3f(1.4,0.8,3.2);
    glVertex3f(1.1,0.8,3.4);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //left
    glNormal3f(-1,0,-0.01);
    glVertex3f(0,0,3.4);
    glVertex3f(0,0.8,3.4);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    //back
    glNormal3f(0.4,0,-1);
    glVertex3f(1.2,0,0.5);
    glVertex3f(1.2,0.8,0.5);
    //right
    glNormal3f(1,0,0);
    glVertex3f(1.2,0,2.85);
    glVertex3f(1.2,0.8,2.85);
    //rightnub
    glNormal3f(0.4,0,-1);
    glVertex3f(1.4,0,3);
    glVertex3f(1.4,0.8,3);

    glNormal3f(1,0,0);
    glVertex3f(1.4,0,3.2);
    glVertex3f(1.4,0.8,3.2);

    glNormal3f(1,0,1);
    glVertex3f(1.1,0,3.4);
    glVertex3f(1.1,0.8,3.4);
    //front
    glNormal3f(0,0,1);
    glVertex3f(0,0,3.4);
    glVertex3f(0,0.8,3.4);
    glEnd();
    glPopMatrix();
}

void blue2(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottomback
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(-0.5,0,0.9);
    glVertex3f(-0.3,0,0.1);
    glVertex3f(0,0,0);
    glVertex3f(0,0,1.1);
    glVertex3f(0.7,0,2.1);
    glVertex3f(0,0,1.8);
    glVertex3f(-0.4,0,1.5);
    glEnd();
    //bottomfront
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(1.2,0,3.6);
    glVertex3f(-0.1,0,2.8);
    glVertex3f(-0.1,0,2.6);
    glVertex3f(0.1,0,2.3);
    glVertex3f(0,0,1.8);
    glVertex3f(0.7,0,2.1);
    glVertex3f(1.9,0,2.8);
    glVertex3f(3.1,0,2.4);
    glVertex3f(3.6,0,2.8);
    glVertex3f(2,0,4.3);
    glEnd();

    //topback
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(-0.5,0.8,0.9);
    glVertex3f(-0.3,0.8,0.1);
    glVertex3f(0,0.8,0);
    glVertex3f(0,0.8,1.1);
    glVertex3f(0.7,0.8,2.1);
    glVertex3f(0,0.8,1.8);
    glVertex3f(-0.4,0.8,1.5);
    glEnd();
    //topfront
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(1.2,0.8,3.6);
    glVertex3f(-0.1,0.8,2.8);
    glVertex3f(-0.1,0.8,2.6);
    glVertex3f(0.1,0.8,2.3);
    glVertex3f(0,0.8,1.8);
    glVertex3f(0.7,0.8,2.1);
    glVertex3f(1.9,0.8,2.8);
    glVertex3f(3.1,0.8,2.4);
    glVertex3f(3.6,0.8,2.8);
    glVertex3f(2,0.8,4.3);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //topcurve
    glNormal3f(1,0,0);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0,0,1.1);
    glVertex3f(0,0.8,1.1);

    glNormal3f(1,0,-1);
    glVertex3f(0.7,0,2.1);
    glVertex3f(0.7,0.8,2.1);

    glNormal3f(0.1,0,-1);
    glVertex3f(1.9,0,2.8);
    glVertex3f(1.9,0.8,2.8);

    glNormal3f(-0.2,0,-1);
    glVertex3f(3.1,0,2.4);
    glVertex3f(3.1,0.8,2.4);

    glNormal3f(0.5,0,-1);
    glVertex3f(3.6,0,2.8);
    glVertex3f(3.6,0.8,2.8);
    //right
    glNormal3f(1,0,1);
    glVertex3f(2,0,4.3);
    glVertex3f(2,0.8,4.3);
    //bottom
    glNormal3f(-0.7,0,1);
    glVertex3f(1.2,0,3.6);
    glVertex3f(1.2,0.8,3.6);

    glNormal3f(-0.1,0,1);
    glVertex3f(-0.1,0,2.8);
    glVertex3f(-0.1,0.8,2.8);
    //left
    glNormal3f(-1,0,0);
    glVertex3f(-0.1,0,2.6);
    glVertex3f(-0.1,0.8,2.6);

    glNormal3f(-1,0,-0.3);
    glVertex3f(0.1,0,2.3);
    glVertex3f(0.1,0.8,2.3);

    glNormal3f(-1,0,0.1);
    glVertex3f(0,0,1.8);
    glVertex3f(0,0.8,1.8);

    glNormal3f(-0.9,0,1);
    glVertex3f(-0.4,0,1.5);
    glVertex3f(-0.4,0.8,1.5);

    glNormal3f(-1,0,0.1);
    glVertex3f(-0.5,0,0.9);
    glVertex3f(-0.5,0.8,0.9);

    glNormal3f(-1,0,-0.3);
    glVertex3f(-0.3,0,0.1);
    glVertex3f(-0.3,0.8,0.1);

    glNormal3f(-0.2,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void blue3(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(1.5,0,0.4);
    glVertex3f(1.6,0,1.1);
    glVertex3f(1.2,0,1.5);
    glVertex3f(0.75,0,1.7);
    glVertex3f(0.4,0,1.5);
    glVertex3f(0.7,0,1);
    glVertex3f(0.65,0,0.8);
    glVertex3f(0.4,0,0.6);
    glVertex3f(-0.6,0,0.6);
    glVertex3f(-0.7,0,0.35);
    glVertex3f(0,0,0);
    glVertex3f(0.8,0,0);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(1.5,0.8,0.4);
    glVertex3f(1.6,0.8,1.1);
    glVertex3f(1.2,0.8,1.5);
    glVertex3f(0.75,0.8,1.7);
    glVertex3f(0.4,0.8,1.5);
    glVertex3f(0.7,0.8,1);
    glVertex3f(0.65,0.8,0.8);
    glVertex3f(0.4,0.8,0.6);
    glVertex3f(-0.6,0.8,0.6);
    glVertex3f(-0.7,0.8,0.35);
    glVertex3f(0,0.8,0);
    glVertex3f(0.8,0.8,0);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //back
    glNormal3f(0,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.8,0,0);
    glVertex3f(0.8,0.8,0);
    //backright
    glNormal3f(0.8,0,-1);
    glVertex3f(1.5,0,0.4);
    glVertex3f(1.5,0.8,0.4);
    //right
    glNormal3f(1,0,-0.05);
    glVertex3f(1.6,0,1.1);
    glVertex3f(1.6,0.8,1.1);
    
    glNormal3f(1,0,1);
    glVertex3f(1.2,0,1.5);
    glVertex3f(1.2,0.8,1.5);

    glNormal3f(0.3,0,1);
    glVertex3f(0.75,0,1.7);
    glVertex3f(0.75,0.8,1.7);
    //front
    glNormal3f(-0.7,0,1);
    glVertex3f(0.4,0,1.5);
    glVertex3f(0.4,0.8,1.5);
    //inside
    glNormal3f(-1,0,-0.5);
    glVertex3f(0.7,0,1);
    glVertex3f(0.7,0.8,1);

    glNormal3f(-1,0,0.1);
    glVertex3f(0.65,0,0.8);
    glVertex3f(0.65,0.8,0.8);

    glNormal3f(-1,0,1);
    glVertex3f(0.4,0,0.6);
    glVertex3f(0.4,0.8,0.6);

    glNormal3f(0,0,1);
    glVertex3f(-0.6,0,0.6);
    glVertex3f(-0.6,0.8,0.6);
    //left
    glNormal3f(-1,0,0.2);
    glVertex3f(-0.7,0,0.35);
    glVertex3f(-0.7,0.8,0.35);

    glNormal3f(-0.7,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void blue4(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(2,0,1.2);
    glVertex3f(0.6,0,2.5);
    glVertex3f(-0.1,0,2.525);
    glVertex3f(0,0,2.4);
    glVertex3f(0,0,1.6);
    glVertex3f(-0.25,0,0.7);
    glVertex3f(0,0,0);
    glVertex3f(0.8,0,0);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(2,0.8,1.2);
    glVertex3f(0.6,0.8,2.5);
    glVertex3f(0.1,0.8,2.525);
    glVertex3f(0,0.8,2.4);
    glVertex3f(0,0.8,1.6);
    glVertex3f(-0.25,0.8,0.7);
    glVertex3f(0,0.8,0);
    glVertex3f(0.8,0.8,0);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //back
    glNormal3f(0,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.8,0,0);
    glVertex3f(0.8,0.8,0);
    //rightback
    glNormal3f(0.95,0,-1);
    glVertex3f(2,0,1.2);
    glVertex3f(2,0.8,1.2);
    //rightfront
    glNormal3f(0.95,0,1);
    glVertex3f(0.6,0,2.5);
    glVertex3f(0.6,0.8,2.5);
    //bottom
    glNormal3f(0.05,0,1);
    glVertex3f(0.1,0,2.525);
    glVertex3f(0.1,0.8,2.525);

    glNormal3f(-1,0,1);
    glVertex3f(0,0,2.4);
    glVertex3f(0,0.8,2.4);
    //left
    glNormal3f(-1,0,-0.3);
    glVertex3f(0,0,1.6);
    glVertex3f(0,0.8,1.6);

    glNormal3f(-1,0,0.3);
    glVertex3f(-0.25,0,0.7);
    glVertex3f(-0.25,0.8,0.7);

    glNormal3f(-1,0,-0.9);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void blue5(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottomback
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(0.7,0,-0.4);
    glVertex3f(1.1,0,-0.45);
    glVertex3f(1.1,0,-0.15);
    glVertex3f(0.4,0,0.5);
    glVertex3f(0.45,0,1.2);
    glVertex3f(0.05,0,1.9);
    glEnd();
    //bottomfront
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0.05,0,1.9);
    glVertex3f(0.45,0,1.2);
    glVertex3f(1,0,1.8);
    glVertex3f(1.6,0,1.9);
    glVertex3f(2,0,2.3);
    glVertex3f(1.85,0,2.6);
    glVertex3f(1.6,0,2.7);
    glEnd();

    //topback
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.7,0.8,-0.4);
    glVertex3f(1.1,0.8,-0.45);
    glVertex3f(1.1,0.8,-0.15);
    glVertex3f(0.4,0.8,0.5);
    glVertex3f(0.45,0.8,1.2);
    glVertex3f(0.05,0.8,1.9);
    glEnd();
    //topfront
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0.05,0.8,1.9);
    glVertex3f(0.45,0.8,1.2);
    glVertex3f(1,0.8,1.8);
    glVertex3f(1.6,0.8,1.9);
    glVertex3f(2,0.8,2.3);
    glVertex3f(1.85,0.8,2.6);
    glVertex3f(1.6,0.8,2.7);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //back
    glNormal3f(-0.5,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.7,0,-0.4);
    glVertex3f(0.7,0.8,-0.4);

    glNormal3f(-0.1,0,-1);
    glVertex3f(1.1,0,-0.45);
    glVertex3f(1.1,0.8,-0.45);
    //rightback
    glNormal3f(1,0,0);
    glVertex3f(1.1,0,-0.15);
    glVertex3f(1.1,0.8,-0.15);
    //rightin
    glNormal3f(1,0,1);
    glVertex3f(0.4,0,0.5);
    glVertex3f(0.4,0.8,0.5);

    glNormal3f(1,0,-0.1);
    glVertex3f(0.45,0,1.2);
    glVertex3f(0.45,0.8,1.2);

    glNormal3f(1,0,-1);
    glVertex3f(1,0,1.8);
    glVertex3f(1,0.8,1.8);

    glNormal3f(0.1,0,-1);
    glVertex3f(1.6,0,1.9);
    glVertex3f(1.6,0.8,1.9);
    //rightfront
    glNormal3f(1,0,-1);
    glVertex3f(2,0,2.3);
    glVertex3f(2,0.8,2.3);

    glNormal3f(1,0,0.7);
    glVertex3f(1.85,0,2.6);
    glVertex3f(1.85,0.8,2.6);

    glNormal3f(0.3,0,1);
    glVertex3f(1.6,0,2.7);
    glVertex3f(1.6,0.8,2.7);
    //bottom
    glNormal3f(-0.4,0,1);
    glVertex3f(0.05,0,1.9);
    glVertex3f(0.05,0.8,1.9);
    //left
    glNormal3f(-1,0,0.01);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void blue6(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,2.3);
    glVertex3f(-0.1,0,1.9);
    glVertex3f(0,0,0);
    glVertex3f(0.1,0,-0.1);
    glVertex3f(0.35,0,0.2);
    glVertex3f(0.4,0,1);
    glVertex3f(0.7,0,1.8);
    glVertex3f(1.4,0,2.1);
    glVertex3f(1.5,0,2.4);
    glVertex3f(1,0,2.6);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,2.3);
    glVertex3f(-0.1,0.8,1.9);
    glVertex3f(0,0.8,0);
    glVertex3f(0.1,0.8,-0.1);
    glVertex3f(0.35,0.8,0.2);
    glVertex3f(0.4,0.8,1);
    glVertex3f(0.7,0.8,1.8);
    glVertex3f(1.4,0.8,2.1);
    glVertex3f(1.5,0.8,2.4);
    glVertex3f(1,0.8,2.6);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //top
    glNormal3f(-1,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.1,0,-0.1);
    glVertex3f(0.1,0.8,-0.1);

    glNormal3f(1,0,-1);
    glVertex3f(0.35,0,0.2);
    glVertex3f(0.35,0.8,0.2);
    //rightin
    glNormal3f(1,0,-0.01);
    glVertex3f(0.4,0,1);
    glVertex3f(0.4,0.8,1);

    glNormal3f(1,0,-0.3);
    glVertex3f(0.7,0,1.8);
    glVertex3f(0.7,0.8,1.8);

    glNormal3f(0.2,0,-1);
    glVertex3f(1.4,0,2.1);
    glVertex3f(1.4,0.8,2.1);
    //right
    glNormal3f(1,0,-0.2);
    glVertex3f(1.5,0,2.4);
    glVertex3f(1.5,0.8,2.4);

    glNormal3f(0.8,0,1);
    glVertex3f(1,0,2.6);
    glVertex3f(1,0.8,2.6);
    //bottom
    glNormal3f(-0.2,0,1);
    glVertex3f(0,0,2.3);
    glVertex3f(0,0.8,2.3);
    //left
    glNormal3f(-1,0,0.1);
    glVertex3f(-0.1,0,1.9);
    glVertex3f(-0.1,0.8,1.9);

    glNormal3f(-1,0,-0.01);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void blue7(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottomback
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0.95,0,1.45);
    glVertex3f(0.95,0,2.1);
    glVertex3f(0.4,0,2.5);
    glVertex3f(0.35,0,2.2);
    glVertex3f(0.1,0,2);
    glVertex3f(-0.2,0,1.9);
    glVertex3f(-0.15,0,1.1);
    glVertex3f(-0.3,0,0.6);
    glVertex3f(0,0,0);
    glVertex3f(0.9,0,0.7);
    glVertex3f(1.4,0,0.95);
    glVertex3f(1.4,0,1.15);
    glEnd();
    //bottomfront
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0.6,0,3.35);
    glVertex3f(-1.4,0,3.2);
    glVertex3f(-1.45,0,3.05);
    glVertex3f(-1.4,0,2.9);
    glVertex3f(-0.8,0,2.9);
    glVertex3f(-0.3,0,3);
    glVertex3f(0.2,0,2.9);
    glVertex3f(0.4,0,2.5);
    glVertex3f(0.95,0,2.1);
    glVertex3f(1.3,0,2.7);
    glVertex3f(1.1,0,3.2);
    glEnd();
    //topback
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0.95,0.8,1.45);
    glVertex3f(0.95,0.8,2.1);
    glVertex3f(0.4,0.8,2.5);
    glVertex3f(0.35,0.8,2.2);
    glVertex3f(0.1,0.8,2);
    glVertex3f(-0.2,0.8,1.9);
    glVertex3f(-0.15,0.8,1.1);
    glVertex3f(-0.3,0.8,0.6);
    glVertex3f(0,0.8,0);
    glVertex3f(0.9,0.8,0.7);
    glVertex3f(1.4,0.8,0.95);
    glVertex3f(1.4,0.8,1.15);
    glEnd();
    //topfront
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0.6,0.8,3.35);
    glVertex3f(-1.4,0.8,3.2);
    glVertex3f(-1.45,0.8,3.05);
    glVertex3f(-1.4,0.8,2.9);
    glVertex3f(-0.8,0.8,2.9);
    glVertex3f(-0.3,0.8,3);
    glVertex3f(0.2,0.8,2.9);
    glVertex3f(0.4,0.8,2.5);
    glVertex3f(0.95,0.8,2.1);
    glVertex3f(1.3,0.8,2.7);
    glVertex3f(1.1,0.8,3.2);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //top
    glNormal3f(0.8,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.9,0,0.7);
    glVertex3f(0.9,0.8,0.7);

    glNormal3f(0.2,0,-1);
    glVertex3f(1.4,0,0.95);
    glVertex3f(1.4,0.8,0.95);
    //righttop
    glNormal3f(1,0,0);
    glVertex3f(1.4,0,1.15);
    glVertex3f(1.4,0.8,1.15);
    //rightin
    glNormal3f(0.2,0,1);
    glVertex3f(0.95,0,1.45);
    glVertex3f(0.95,0.8,1.45);

    glNormal3f(1,0,0);
    glVertex3f(0.95,0,2.1);
    glVertex3f(0.95,0.8,2.1);

    glNormal3f(1,0,-1);
    glVertex3f(1.3,0,2.7);
    glVertex3f(1.3,0.8,2.7);
    //bottom
    glNormal3f(1,0,0.9);
    glVertex3f(1.1,0,3.2);
    glVertex3f(1.1,0.8,3.2);

    glNormal3f(0.1,0,1);
    glVertex3f(0.6,0,3.35);
    glVertex3f(0.6,0.8,3.35);

    glNormal3f(-0.01,0,1);
    glVertex3f(-1.4,0,3.2);
    glVertex3f(-1.4,0.8,3.2);
    //lefttip
    glNormal3f(-1,0,0.1);
    glVertex3f(-1.45,0,3.05);
    glVertex3f(-1.45,0.8,3.05);

    glNormal3f(-1,0,-0.1);
    glVertex3f(-1.4,0,2.9);
    glVertex3f(-1.4,0.8,2.9);
    //leftin
    glNormal3f(0,0,-1);
    glVertex3f(-0.8,0,2.9);
    glVertex3f(-0.8,0.8,2.9);

    glNormal3f(0.1,0,-1);
    glVertex3f(-0.3,0,3);
    glVertex3f(-0.3,0.8,3);

    glNormal3f(-0.1,0,-1);
    glVertex3f(0.2,0,2.9);
    glVertex3f(0.2,0.8,2.9);

    glNormal3f(-1,0,-0.2);
    glVertex3f(0.4,0,2.5);
    glVertex3f(0.4,0.8,2.5);

    glNormal3f(-1,0,0.02);
    glVertex3f(0.35,0,2.2);
    glVertex3f(0.35,0.8,2.2);

    glNormal3f(-0.8,0,1);
    glVertex3f(0.1,0,2);
    glVertex3f(0.1,0.8,2);

    glNormal3f(-0.1,0,1);
    glVertex3f(-0.2,0,1.9);
    glVertex3f(-0.2,0.8,1.9);
    //left
    glNormal3f(-1,0,-0.01);
    glVertex3f(-0.15,0,1.1);
    glVertex3f(-0.15,0.8,1.1);

    glNormal3f(-1,0,0.2);
    glVertex3f(-0.3,0,0.6);
    glVertex3f(-0.3,0.8,0.6);

    glNormal3f(-1,0,-0.7);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void blue8(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottomtop
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(0.5,0,0);
    glVertex3f(1.1,0,0.3);
    glVertex3f(2,0,1.6);
    glVertex3f(1.7,0,2.3);
    glVertex3f(1.1,0,2);
    glVertex3f(0.4,0,2.1);
    glVertex3f(-0.15,0,2.6);
    glVertex3f(-0.65,0,3);
    glVertex3f(-0.6,0,0.8);
    glEnd();
    //bottombottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(2.7,0,3.3);
    glVertex3f(2.75,0,3.8);
    glVertex3f(2.4,0,4.2);
    glVertex3f(2.4,0,3.7);
    glVertex3f(1.7,0,2.3);
    glVertex3f(2,0,1.6);
    glEnd();
    //toptop
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.5,0.8,0);
    glVertex3f(1.1,0.8,0.3);
    glVertex3f(2,0.8,1.6);
    glVertex3f(1.7,0.8,2.3);
    glVertex3f(1.1,0.8,2);
    glVertex3f(0.4,0.8,2.1);
    glVertex3f(-0.15,0.8,2.6);
    glVertex3f(-0.65,0.8,3);
    glVertex3f(-0.6,0.8,0.8);
    glEnd();
    //topbottom
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(2.7,0.8,3.3);
    glVertex3f(2.75,0.8,3.8);
    glVertex3f(2.4,0.8,4.2);
    glVertex3f(2.4,0.8,3.7);
    glVertex3f(1.7,0.8,2.3);
    glVertex3f(2,0.8,1.6);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //top
    glNormal3f(0,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.5,0,0);
    glVertex3f(0.5,0.8,0);

    glNormal3f(0.8,0,-1);
    glVertex3f(1.1,0,0.3);
    glVertex3f(1.1,0.8,0.3);
    //right
    glNormal3f(1,0,-0.6);
    glVertex3f(2,0,1.6);
    glVertex3f(2,0.8,1.6);

    glNormal3f(1,0,-0.3);
    glVertex3f(2.7,0,3.3);
    glVertex3f(2.7,0.8,3.3);

    glNormal3f(1,0,-0.05);
    glVertex3f(2.75,0,3.8);
    glVertex3f(2.75,0.8,3.8);

    glNormal3f(1,0,1);
    glVertex3f(2.4,0,4.2);
    glVertex3f(2.4,0.8,4.2);
    //in
    glNormal3f(-1,0,0);
    glVertex3f(2.4,0,3.7);
    glVertex3f(2.4,0.8,3.7);

    glNormal3f(-1,0,0.6);
    glVertex3f(1.7,0,2.3);
    glVertex3f(1.7,0.8,2.3);

    glNormal3f(-0.1,0,1);
    glVertex3f(1.1,0,2);
    glVertex3f(1.1,0.8,2);

    glNormal3f(0.1,0,1);
    glVertex3f(0.4,0,2.1);
    glVertex3f(0.4,0.8,2.1);

    glNormal3f(0.9,0,1);
    glVertex3f(-0.15,0,2.6);
    glVertex3f(-0.15,0.8,2.6);

    glNormal3f(0.2,0,1);
    glVertex3f(-0.65,0,3);
    glVertex3f(-0.65,0.8,3);
    //left
    glNormal3f(-1,0,-0.01);
    glVertex3f(-0.6,0,0.8);
    glVertex3f(-0.6,0.8,0.8);

    glNormal3f(-1,0,-0.9);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void river1(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);
    
    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(0.9,0,0.2);
    glVertex3f(2.1,0,0.9);
    glVertex3f(2.8,0,1.8);
    glVertex3f(2.3,0,2.3);
    glVertex3f(1,0,1.2);
    glVertex3f(0,0,0.8);
    glVertex3f(-0.85,0,0.7);
    glVertex3f(-0.9,0,0.5);
    glVertex3f(-0.5,0,0.1);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(0.9,0.8,0.2);
    glVertex3f(2.1,0.8,0.9);
    glVertex3f(2.8,0.8,1.8);
    glVertex3f(2.3,0.8,2.3);
    glVertex3f(1,0.8,1.2);
    glVertex3f(0,0.8,0.8);
    glVertex3f(-0.85,0.8,0.7);
    glVertex3f(-0.9,0.8,0.5);
    glVertex3f(-0.5,0.8,0.1);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //top
    glNormal3f(-1,0,-1);
    glVertex3f(-0.9,0,0.5);
    glVertex3f(-0.9,0.8,0.5);
    glVertex3f(-0.5,0,0.1);
    glVertex3f(-0.5,0.8,0.1);

    glNormal3f(-0.1,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);

    glNormal3f(0.2,0,-1);
    glVertex3f(0.9,0,0.2);
    glVertex3f(0.9,0.8,0.2);

    glNormal3f(0.5,0,-1);
    glVertex3f(2.1,0,0.9);
    glVertex3f(2.1,0.8,0.9);
    //right
    glNormal3f(1,0,-0.8);
    glVertex3f(2.8,0,1.8);
    glVertex3f(2.8,0.8,1.8);

    glNormal3f(1,0,1);
    glVertex3f(2.3,0,2.3);
    glVertex3f(2.3,0.8,2.3);
    //bottom
    glNormal3f(-0.8,0,1);
    glVertex3f(1,0,1.2);
    glVertex3f(1,0.8,1.2);

    glNormal3f(-0.3,0,1);
    glVertex3f(0,0,0.8);
    glVertex3f(0,0.8,0.8);

    glNormal3f(-0.1,0,1);
    glVertex3f(-0.85,0,0.7);
    glVertex3f(-0.85,0.8,0.7);
    //left
    glNormal3f(-1,0,0.2);
    glVertex3f(-0.9,0,0.5);
    glVertex3f(-0.9,0.8,0.5);
    glEnd();
    glPopMatrix();
}

void river2(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);
    
    //bottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(-0.5,0,-0.5);
    glVertex3f(0.8,0,0);
    glVertex3f(2.55,0,1.25);
    glVertex3f(2.2,0,1.6);
    glVertex3f(-0.1,0,0.1);
    glEnd();
    //top
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(-0.5,0.8,-0.5);
    glVertex3f(0.8,0.8,0);
    glVertex3f(2.55,0.8,1.25);
    glVertex3f(2.2,0.8,1.6);
    glVertex3f(-0.1,0.8,0.1);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //top
    glNormal3f(0,0,-1);
    glVertex3f(-0.5,0,-0.5);
    glVertex3f(-0.5,0.8,-0.5);
    glVertex3f(0.8,0,0);
    glVertex3f(0.8,0.8,0);
    //right
    glNormal3f(0.9,0,-1);
    glVertex3f(2.55,0,1.25);
    glVertex3f(2.55,0.8,1.25);

    glNormal3f(0.9,0,1);
    glVertex3f(2.2,0,1.6);
    glVertex3f(2.2,0.8,1.6);
    //bottom
    glNormal3f(-0.9,0,1);
    glVertex3f(-0.1,0,0.1);
    glVertex3f(-0.1,0.8,0.1);
    //left
    glNormal3f(-1,0,-0.2);
    glVertex3f(-0.5,0,-0.5);
    glVertex3f(-0.5,0.8,-0.5);
    glEnd();
    glPopMatrix();
}

void pit(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);

    //bottomtop
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(0,0,0);
    glVertex3f(2.2,0,0.8);
    glVertex3f(2.2,0,1);
    glVertex3f(1.5,0,1);
    glVertex3f(1,0,1.2);
    glVertex3f(0.8,0,1.8);
    glVertex3f(0.35,0,1.1);
    glVertex3f(-0.1,0,0.6);
    glVertex3f(-0.5,0,0.5);
    glVertex3f(-0.6,0,0.25);
    glVertex3f(-0.3,0,0);
    glEnd();
    //bottommiddle
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(1.2,0,3.1);
    glVertex3f(0.7,0,2.6);
    glVertex3f(0.3,0,1.9);
    glVertex3f(0.35,0,1.1);
    glVertex3f(0.8,0,1.8);
    glVertex3f(1.2,0,2.4);
    glVertex3f(1.6,0,2.8);
    glVertex3f(2,0,2.9);
    glVertex3f(2.8,0,2.8);
    glVertex3f(3.6,0,3.15);
    glVertex3f(3.2,0,3.8);
    glVertex3f(3,0,3.85);
    glVertex3f(2.1,0,3.6);
    glVertex3f(1.2,0,3.1);
    glEnd();
    //bottombottom
    glBegin(GL_POLYGON);
    glNormal3f(0,-1,0);
    glVertex3f(4.4,0,3.1);
    glVertex3f(4.25,0,3.2);
    glVertex3f(3.6,0,3.15);
    glVertex3f(2.8,0,2.8);
    glVertex3f(3.1,0,2.3);
    glVertex3f(3.2,0,1.7);
    glVertex3f(3.1,0,1.5);
    glVertex3f(3.3,0,1.3);
    glVertex3f(4.3,0,2.2);
    glEnd();

    //toptop
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(0,0.8,0);
    glVertex3f(2.2,0.8,0.8);
    glVertex3f(2.2,0.8,1);
    glVertex3f(1.5,0.8,1);
    glVertex3f(1,0.8,1.2);
    glVertex3f(0.8,0.8,1.8);
    glVertex3f(0.35,0.8,1.1);
    glVertex3f(-0.1,0.8,0.6);
    glVertex3f(-0.5,0.8,0.5);
    glVertex3f(-0.6,0.8,0.25);
    glVertex3f(-0.3,0.8,0);
    glEnd();
    //topmiddle
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(1.2,0.8,3.1);
    glVertex3f(0.7,0.8,2.6);
    glVertex3f(0.3,0.8,1.9);
    glVertex3f(0.35,0.8,1.1);
    glVertex3f(0.8,0.8,1.8);
    glVertex3f(1.2,0.8,2.4);
    glVertex3f(1.6,0.8,2.8);
    glVertex3f(2,0.8,2.9);
    glVertex3f(2.8,0.8,2.8);
    glVertex3f(3.6,0.8,3.15);
    glVertex3f(3.2,0.8,3.8);
    glVertex3f(3,0.8,3.85);
    glVertex3f(2.1,0.8,3.6);
    glVertex3f(1.2,0.8,3.1);
    glEnd();
    //topbottom
    glBegin(GL_POLYGON);
    glNormal3f(0,1,0);
    glVertex3f(4.4,0.8,3.1);
    glVertex3f(4.25,0.8,3.2);
    glVertex3f(3.6,0.8,3.15);
    glVertex3f(2.8,0.8,2.8);
    glVertex3f(3.1,0.8,2.3);
    glVertex3f(3.2,0.8,1.7);
    glVertex3f(3.1,0.8,1.5);
    glVertex3f(3.3,0.8,1.3);
    glVertex3f(4.3,0.8,2.2);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    //top
    glNormal3f(0.4,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glVertex3f(2.2,0,0.8);
    glVertex3f(2.2,0.8,0.8);

    glNormal3f(1,0,0);
    glVertex3f(2.2,0,1);
    glVertex3f(2.2,0.8,1);
    //inleft
    glNormal3f(0,0,1);
    glVertex3f(1.5,0,1);
    glVertex3f(1.5,0.8,1);

    glNormal3f(0.4,0,1);
    glVertex3f(1,0,1.2);
    glVertex3f(1,0.8,1.2);

    glNormal3f(1,0,0.3);
    glVertex3f(0.8,0,1.8);
    glVertex3f(0.8,0.8,1.8);

    glNormal3f(1,0,-0.8);
    glVertex3f(1.2,0,2.4);
    glVertex3f(1.2,0.8,2.4);

    glNormal3f(1,0,-0.9);
    glVertex3f(1.6,0,2.8);
    glVertex3f(1.6,0.8,2.8);

    glNormal3f(0.2,0,-1);
    glVertex3f(2,0,2.9);
    glVertex3f(2,0.8,2.9);
    //inright
    glNormal3f(-0.1,0,-1);
    glVertex3f(2.8,0,2.8);
    glVertex3f(2.8,0.8,2.8);

    glNormal3f(-1,0,-0.5);
    glVertex3f(3.1,0,2.3);
    glVertex3f(3.1,0.8,2.3);

    glNormal3f(-1,0,-0.2);
    glVertex3f(3.2,0,1.7);
    glVertex3f(3.2,0.8,1.7);

    glNormal3f(-1,0,0.8);
    glVertex3f(3.1,0,1.5);
    glVertex3f(3.1,0.8,1.5);

    glNormal3f(-1,0,-1);
    glVertex3f(3.3,0,1.3);
    glVertex3f(3.3,0.8,1.3);

    glNormal3f(0.9,0,-1);
    glVertex3f(4.3,0,2.2);
    glVertex3f(4.3,0.8,2.2);
    //right
    glNormal3f(1,0,-0.1);
    glVertex3f(4.4,0,3.1);
    glVertex3f(4.4,0.8,3.1);

    glNormal3f(0.9,0,1);
    glVertex3f(4.25,0,3.2);
    glVertex3f(4.25,0.8,3.2);
    //bottomright
    glNormal3f(-0.1,0,1);
    glVertex3f(3.6,0,3.15);
    glVertex3f(3.6,0.8,3.15);

    glNormal3f(1,0,0.5);
    glVertex3f(3.2,0,3.8);
    glVertex3f(3.2,0.8,3.8);

    glNormal3f(0.3,0,1);
    glVertex3f(3,0,3.85);
    glVertex3f(3,0.8,3.85);
    //bottommiddle
    glNormal3f(-0.3,0,1);
    glVertex3f(2.1,0,3.6);
    glVertex3f(2.1,0.8,3.6);

    glNormal3f(-0.6,0,1);
    glVertex3f(1.2,0,3.1);
    glVertex3f(1.2,0.8,3.1);
    //bottomleft
    glNormal3f(-1,0,1);
    glVertex3f(0.7,0,2.6);
    glVertex3f(0.7,0.8,2.6);

    glNormal3f(-1,0,0.8);
    glVertex3f(0.3,0,1.9);
    glVertex3f(0.3,0.8,1.9);
    //left
    glNormal3f(-1,0,-0.1);
    glVertex3f(0.35,0,1.1);
    glVertex3f(0.35,0.8,1.1);

    glNormal3f(-0.9,0,1);
    glVertex3f(-0.1,0,0.6);
    glVertex3f(-0.1,0.8,0.6);

    glNormal3f(-0.2,0,1);
    glVertex3f(-0.5,0,0.5);
    glVertex3f(-0.5,0.8,0.5);

    glNormal3f(-1,0,0.4);
    glVertex3f(-0.6,0,0.25);
    glVertex3f(-0.6,0.8,0.25);

    glNormal3f(-0.9,0,-1);
    glVertex3f(-0.3,0,0);
    glVertex3f(-0.3,0.8,0);

    glNormal3f(0,0,-1);
    glVertex3f(0,0,0);
    glVertex3f(0,0.8,0);
    glEnd();
    glPopMatrix();
}

void tower(double x, double y, double z, double th){
    float white[] = {1,1,1,1};
    float black[] = {0,0,0,1};
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shiny);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,white);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,black);
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);
    glColor3f(0.5,0.5,0.5);
    
    glBegin(GL_POLYGON);
    
    glEnd();
    glPopMatrix();
}

void riftHalf(double x, double y, double z, double th){
    glPushMatrix();
    glTranslated(x,y,z);
    glRotated(th,0,1,0);

    baseWalls(9.5,0,-2.5,90,0);
    baseWalls(9.25,0,-5,135,0);
    red1(9.25,0,-7.2,45);
    red2(11.75,0,-5.5,0);
    red3(11.75,0,-9.75,45);
    red4(12.75,0,-6,35);
    red5(12.75,0,-3.5,0);
    red6(14.75,0,-7.75,0);
    red7(16,0,-4.75,0);
    red8(16.75,0,-2.75,0);
    red9(19.5,0,-3,0);
    red10(22.5,0,-4.75,0);

    baseWalls(2.5,0.8,-9.5,0,180);
    baseWalls(5,0.8,-9.25,315,180);
    blue1(2.56,0,-14.2,0);
    blue2(5.2,0,-13.5,0);
    blue3(6.8,0,-13.3,0);
    blue4(9.25,0,-14.25,0);
    blue5(2.56,0,-17.25,0);
    blue6(4.5,0,-18.35,0);
    blue7(7.25,0,-17.5,0);
    blue8(3.35,0,-21.75,0);

    river1(10,0,-16.25,0);
    river2(13,0,-19,0);

    pit(18.5,0,-9.5,0);

    glPopMatrix();
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
    gluLookAt(Ex+eyeX,Ey,Ez-eyeZ , 0+eyeX,0,0-eyeZ , 0,Cos(ph),0);
    glShadeModel(GL_SMOOTH);
    if(lightOn){
        float Ambient[] = {0.01*ambient ,0.01*ambient ,0.01*ambient ,1.0};
        float Diffuse[] = {0.01*diffuse ,0.01*diffuse ,0.01*diffuse ,1.0};
        float Specular[] = {0.01*specular,0.01*specular,0.01*specular,1.0};

        float Position[]  = {distance*Cos(zh),ylight,distance*Sin(zh),1.0};
        glColor3f(1,1,1);
        if(mode != 22) {
            distance = 5;
            Position[0] = distance*Cos(zh);
            Position[2] = distance*Sin(zh);
            ball(Position[0],Position[1],Position[2] , 0.1);
        }
        if(mode == 22){
            distance = 10;
            Position[0] = 15+distance*Cos(zh);
            Position[2] = -15+distance*Sin(zh);
            ball(Position[0],Position[1],Position[2] , 0.1);
        }
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
    // Ground();
    // Krug(3,0,0,1,1,1,-5); //main big krug
    // Krug(0,-0.225,0,0.75,0.75,0.75,10); //medium krug
    // Krug(-0.5,-0.55,2.5,0.4,0.4,0.4,45); //small krugs
    // Krug(1.5,-0.55,1.75,0.4,0.4,0.4,5);
    // Walls();

    switch (mode)
    {
    case 0:
        eyeX=0, eyeZ=0;
        baseWalls(0,0,0,0,0);
        break;
    case 1:
        eyeX=0, eyeZ=0;
        red1(0,0,0,0);
        break;
    case 2:
        eyeX=0, eyeZ=0;
        red2(0,0,0,0);
        break;
    case 3:
        eyeX=0, eyeZ=0;
        red3(0,0,0,0);
        break;
    case 4:
        eyeX=0, eyeZ=0;
        red4(0,0,0,0);
        break;
    case 5:
        eyeX=0, eyeZ=0;
        red5(0,0,0,0);
        break;
    case 6:
        eyeX=0, eyeZ=0;
        red6(0,0,0,0);
        break;
    case 7:
        eyeX=0, eyeZ=0;
        red7(0,0,0,0);
        break;
    case 8:
        eyeX=0, eyeZ=0;
        red8(0,0,0,0);
        break;
    case 9:
        eyeX=0, eyeZ=0;
        red9(0,0,0,0);
        break;
    case 10:
        eyeX=0, eyeZ=0;
        red10(0,0,0,0);
        break;
    case 11:
        eyeX=0, eyeZ=0;
        blue1(0,0,0,0);
        break;
    case 12:
        eyeX=0, eyeZ=0;
        blue2(0,0,0,0);
        break;
    case 13:
        eyeX=0, eyeZ=0;
        blue3(0,0,0,0);
        break;
    case 14:
        eyeX=0, eyeZ=0;
        blue4(0,0,0,0);
        break;
    case 15:
        eyeX=0, eyeZ=0;
        blue5(0,0,0,0);
        break;
    case 16:
        eyeX=0, eyeZ=0;
        blue6(0,0,0,0);
        break;
    case 17:
        eyeX=0, eyeZ=0;
        blue7(0,0,0,0);
        break;
    case 18:
        eyeX=0, eyeZ=0;
        blue8(0,0,0,0);
        break;
    case 19:
        eyeX=0, eyeZ=0;
        river1(0,0,0,0);
        break;
    case 20:
        eyeX=0, eyeZ=0;
        river2(0,0,0,0);
        break;
    case 21:
        eyeX=0, eyeZ=0;
        pit(0,0,0,0);
        break;
    default:
        betterGround();
        //blueside
        riftHalf(0,0,0,0);
        //redside
        riftHalf(30,0,-30,180);
        break;
    }
    
    glColor3f(1,1,1);
    glWindowPos2i(5,5);
    Print("Angle=%d,%d Center=%.1f,%.1f Dim=%.1f FOV=%d Light Height=%.1f Mode=%d",th,ph,eyeX,eyeZ,dim,fov,ylight,mode);
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
    else if (ch == '1') //stop/start light movement
        move = 1-move;
    else if (ch == '.') //move light manually
        zh += 1;
    else if (ch == ',')
        zh -= 1;
    else if (ch == '9') //light height
        ylight -= 0.1;
    else if (ch == '0')
        ylight += 0.1;
    else if (ch == 'm'){
        if(mode<=21) mode++;
        else mode = 0;
    }
    else if (ch == 'w') eyeZ+=0.25;
    else if (ch == 's') eyeZ-=0.25;
    else if (ch == 'a') eyeX-=0.25;
    else if (ch == 'd') eyeX+=0.25;
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
