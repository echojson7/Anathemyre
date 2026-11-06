// Модель Вернера: плиты песка, скачки по ветру (два ветра), тень 15°, осыпание 34° по 8 соседям
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#ifndef N
#define N 512
#endif
static int16_t h[N*N];
static uint64_t s=0x9E3779B97F4A7C15ull;
static inline uint32_t rnd(){ s^=s<<13; s^=s>>7; s^=s<<17; return (uint32_t)(s>>32);}
static inline int W(int x){return (x+N)&(N-1);}
static int SH_D=0; // глубина поиска тени
static inline int in_shadow(int x,int y,int dir){
  int hh=h[y*N+x];
  // tan15° ≈ 0.268 клетки на клетку = 0.8 плиты (плита = 1/3 клетки): 4 плиты на 5 клеток
  for(int d=1; d<SH_D; d++){ int xu= dir==0?W(x-d):x, yu= dir==0?y:W(y-d); if(5*(h[yu*N+xu]-hh) > 4*d) return 1; }
  return 0;
}
static const int DX[8]={1,-1,0,0,1,1,-1,-1}, DY[8]={0,0,1,-1,1,-1,1,-1};
static void slide_down(int x,int y){
  for(int it=0; it<64; it++){
    int c=y*N+x, best=-1, bd=0, k0=rnd()&7;
    for(int kk=0;kk<8;kk++){int k=(kk+k0)&7; int d=h[c]-h[W(y+DY[k])*N+W(x+DX[k])]; int th=k<4?2:3; if(d>th && d-th>bd){bd=d-th;best=k;}}
    if(best<0) return;
    h[c]--; x=W(x+DX[best]); y=W(y+DY[best]); h[y*N+x]++;
  }
}
static void slide_in(int x,int y){
  for(int it=0; it<64; it++){
    int c=y*N+x, best=-1, bd=0, k0=rnd()&7;
    for(int kk=0;kk<8;kk++){int k=(kk+k0)&7; int d=h[W(y+DY[k])*N+W(x+DX[k])]-h[c]; int th=k<4?2:3; if(d>th && d-th>bd){bd=d-th;best=k;}}
    if(best<0) return;
    int nx=W(x+DX[best]), ny=W(y+DY[best]); h[ny*N+nx]--; h[c]++; x=nx; y=ny;
  }
}
// плавный шум для толщины песка
static float vn(float x,float y,int p,uint32_t seed){
  int xi=(int)floorf(x), yi=(int)floorf(y); float fx=x-xi, fy=y-yi;
  fx=fx*fx*(3-2*fx); fy=fy*fy*(3-2*fy);
  #define HS(a,b) ((float)((((uint32_t)((a)%p+p)%p*73856093u)^((uint32_t)((b)%p+p)%p*19349663u)^seed)*2654435761u>>8)/16777216.0f)
  float a=HS(xi,yi),b=HS(xi+1,yi),c=HS(xi,yi+1),d=HS(xi+1,yi+1);
  return a+(b-a)*fx+(c-a)*fy+(a-b-c+d)*fx*fy;
}
int main(int argc,char**argv){
  int depth=atoi(argv[1]); long iters=atol(argv[2]); int L=atoi(argv[3]); int w2=atoi(argv[4]); // w2 — доля второго ветра из 16
  float var=argc>5?atof(argv[5]):0.6f; SH_D=argc>6?atoi(argv[6]):80;
  const char*out=argc>7?argv[7]:"h.bin";
  for(int y=0;y<N;y++)for(int x=0;x<N;x++){
    float n=0.6f*vn(x*4.0f/N,y*4.0f/N,4,1)+0.4f*vn(x*8.0f/N,y*8.0f/N,8,2);
    float dd=depth*(1.0f-var+2*var*n);
    h[y*N+x]=(int16_t)(dd+(rnd()%3));
  }
  long cells=(long)N*N;
  for(long it=0; it<iters*cells; it++){
    int dir = (int)(rnd()&15) < w2 ? 1 : 0;
    uint32_t r=rnd(); int x=r%N, y=(r/N)%N;
    int c=y*N+x;
    if(h[c]<=0) continue;
    if(in_shadow(x,y,dir)) continue;
    h[c]--; slide_in(x,y);
    for(int hop=0;hop<400;hop++){
      if(dir==0){ x=W(x+L); uint32_t q=rnd()&7; if(q==0) y=W(y+1); else if(q==1) y=W(y-1);}
      else { y=W(y+L); uint32_t q=rnd()&7; if(q==0) x=W(x+1); else if(q==1) x=W(x-1);}
      int cc=y*N+x;
      if(in_shadow(x,y,dir)){ h[cc]++; slide_down(x,y); break;}
      uint32_t p=rnd()%10; int pd = h[cc]>0 ? 6 : 4;
      if((int)p<pd){ h[cc]++; slide_down(x,y); break;}
    }
    if(it%(cells*100)==0){fprintf(stderr,"%ld ",it/cells);}
  }
  FILE*f=fopen(out,"wb"); fwrite(h,2,N*N,f); fclose(f);
}
