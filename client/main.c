/*
    James William Fletcher (github.com/mrbid)
        November 2022

    To reduce file size the icosphere could be
    generated on program execution by subdividing
    a icosahedron and then snapping the points to
    a unit sphere; and producing an index buffer
    for each triangle from each subdivision.
    
    Get current epoch: date +%s
    Start online game: ./fat <future epoch time> <msaa>

    !! should be rendering exploded last due to opacity,
        and in a seperate array to prevent sorting and
        then painters algo the regular comets array.

    !! I used to be keen on f32 but I tend to use float
    more now just because it's easier to type. I am still
    undecided.
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <pthread.h>
#include <sys/time.h>
#include <sys/file.h>
#include <unistd.h>

#include <errno.h>

//#define VERBOSE
//#define IGNORE_OLD_PACKETS
//#define DEBUG_GL
//#define FAST_START

#pragma GCC diagnostic ignored "-Wunused-result"

#define uint unsigned int
#define sint int
#define f32 GLfloat

#include "inc/gl.h"
#define GLFW_INCLUDE_NONE
#include "inc/glfw3.h"

#ifndef __x86_64__
    #define NOSSE
#endif

#include "inc/esAux2.h"
#include "inc/protocol.h"

#include "inc/res.h"
#include "assets/models.h"
#include "assets/images1.h"
#include "assets/images2.h"
#include "assets/images3.h"
#include "assets/images4.h"

//*************************************
// globals
//*************************************
GLFWwindow* window;
uint winw = 1024;
uint winh = 768;
double t = 0;   // time
f32 dt = 0;     // delta time
double fc = 0;  // frame count
double lfct = 0;// last frame count time
f32 aspect;
double rww, ww, rwh, wh, ww2, wh2;
double uw, uh, uw2, uh2; // normalised pixel dpi
double x,y;

// render state id's
GLint projection_id = -1;
GLint modelview_id;
GLint normalmat_id = -1;
GLint position_id;
GLint lightpos_id;
GLint solidcolor_id;
GLint color_id;
GLint opacity_id;
GLint normal_id;
GLint texcoord_id;
GLint sampler_id;     // diffuse map
GLint specularmap_id; // specular map
GLint normalmap_id;   // normal map

// render state matrices
mat projection;
mat view;
mat model;
mat modelview;

// models
ESModel mdlMenger;
ESModel mdlExo;
ESModel mdlInner;
ESModel mdlRock[9];
ESModel mdlLow;

ESModel mdlUfo;
// ESModel mdlUfoDamaged;
ESModel mdlUfoBeam;
ESModel mdlUfoTri;
ESModel mdlUfoLights;
ESModel mdlUfoShield;

// textures
GLuint tex[17];

// camera vars
#define FAR_DISTANCE 10000.f
vec lightpos = {0.f, 0.f, 0.f};
uint focus_cursor = 0;
double sens = 0.001;

// game vars
#define GFX_SCALE 0.01f
#define MOVE_SPEED 0.5f
#define MAX_PLAYERS 256
#define MAX_COMETS 65535
uint16_t NUM_COMETS = 64;
uint rcs = 0;
f32 rrcs = 0.f;
uint keystate[8] = {0};
vec pp = {0.f, 0.f, 0.f};   // velocity
vec ppr = {0.f, 0.f, -2.3f};// actual position
uint brake = 0;
uint hits = 0;
uint popped = 0;
f32 damage = 0;
f32 sun_pos = 0.f;
time_t sepoch = 0;
uint te[2] = {0};
uint8_t killgame = 0;
pthread_t tid[2];
uint autoroll = 1;
uint ufoind = 0;
f32 ufosht = 0.f;
f32 ufospt = 0.f;
f32 ufonxw = 0.f;

typedef struct
{
    vec vel, newvel, pos, newpos;
    f32 rot, newrot, scale, newscale, is_boom;
} comet;

float players[MAX_PLAYERS*3] = {0};
float pvel[MAX_PLAYERS*3] = {0};
comet comets[MAX_COMETS] = {0};
unsigned char exohit[655362] = {0}; // 0.65 MB

// networking
#define UDP_PORT 8086
#define RECV_BUFF_SIZE 512
char server_host[256];
uint32_t sip = 0; // server ip
const uint64_t pid = 0x75F6073677E10C44; // protocol id
int ssock = -1;
struct sockaddr_in server;
uint8_t myid = 0;
uint64_t latest_packet = 0;
_Atomic uint64_t last_render_time = 0;

//*************************************
// utility functions
//*************************************
void timestamp(char* ts)
{
    const time_t tt = time(0);
    strftime(ts, 16, "%H:%M:%S", localtime(&tt));
}
uint64_t microtime()
{
    struct timeval tv;
    struct timezone tz;
    memset(&tz, 0, sizeof(struct timezone));
    gettimeofday(&tv, &tz);
    return 1000000 * tv.tv_sec + tv.tv_usec;
}
f32 microtimeToScalar(const uint64_t timestamp)
{
    return (float)(int64_t)(last_render_time-timestamp) * 0.000001f;
}
void scaleBuffer(GLfloat* b, GLsizeiptr s)
{
    for(GLsizeiptr i = 0; i < s; i++)
        b[i] *= GFX_SCALE;
}
void flipUV(GLfloat* b, GLsizeiptr s)
{
    for(GLsizeiptr i = 1; i < s; i+=2)
        b[i] *= -1.f;
}
void doExoImpact(vec p, float f)
{
    for(GLsizeiptr h = 0; h < exo_numvert; h++)
    {
        if(exohit[h] == 1){continue;}
        const GLsizeiptr i = h*3;
        vec v = {exo_vertices[i], exo_vertices[i+1], exo_vertices[i+2]};
        f32 ds = vDistSq(v, p);
        if(ds < f*f)
        {
            ds = vDist(v, p);
            vNorm(&v);
            const f32 sr = f-ds;
            vMulS(&v, v, sr);
            if(sr > 0.03f){exohit[h] = 1;}
            exo_vertices[i]   -= v.x;
            exo_vertices[i+1] -= v.y;
            exo_vertices[i+2] -= v.z;
            exo_colors[i]   -= 0.2f;
            exo_colors[i+1] -= 0.2f;
            exo_colors[i+2] -= 0.2f;
        }
    }
    esRebind(GL_ARRAY_BUFFER, &mdlExo.vid, exo_vertices, exo_vertices_size, GL_STATIC_DRAW);
    esRebind(GL_ARRAY_BUFFER, &mdlExo.cid, exo_colors, exo_colors_size, GL_STATIC_DRAW);
}
void incrementHits()
{
    hits++;
    char title[256];
    sprintf(title, "Online Fractal Attack | %u/%u | %.2f%% | %.2f mins", hits, popped, 100.f*damage, (time(0)-sepoch)/60.0);
    glfwSetWindowTitle(window, title);
    if(damage > 10.f)
    {
        killgame = 1;
        sprintf(title, "Online Fractal Attack | %u/%u | 100%% | %.2f mins | GAME END", hits, popped, (time(0)-sepoch)/60.0);
        glfwSetWindowTitle(window, title);
    }
}


//*************************************
// networking functions
//*************************************
// void dumpPacket(const char* buff, const size_t rs)
// {
//     FILE* f = fopen("packet.dat", "w");
//     if(f != NULL)
//     {
//         fwrite(&buff, rs, 1, f);
//         fclose(f);
//     }
//     exit(EXIT_SUCCESS);
// }
uint32_t HOSTtoIPv4(const char* hostname)
{
    struct hostent* host = gethostbyname(hostname);
    if(host == NULL){return -1;}
    struct in_addr** addr = (struct in_addr**)host->h_addr_list;
    for(int i = 0; addr[i] != NULL; i++){return addr[i]->s_addr;}
    return 0;
}

int csend(const unsigned char* data, const size_t len)
{
    uint retry = 0;
    while(sendto(ssock, data, len, 0, (struct sockaddr*)&server, sizeof(server)) < 0)
    {
        retry++;
        if(retry > 3)
        {
            printf("csend() failed.\n");
            printf("ERROR: %s\n", strerror(errno));
            break;
        }
        usleep(100000); // 100ms
    }
    return 0;
}

void *sendThread(void *arg)
{
    // dont send until 1 second before game start
    //sleep(time(0)-sepoch-1);

    // send position on interval until end game
    const size_t packet_len = 41;
    unsigned char packet[packet_len];
    memcpy(&packet, &pid, sizeof(uint64_t));
    memcpy(&packet[8], &(uint8_t){MSG_TYPE_PLAYER_POS}, sizeof(uint8_t));
    const size_t poslen = sizeof(float)*3;
    const useconds_t st = 50000;
    useconds_t cst = st;
    const uint retries = 3;
    const useconds_t rst = st / retries;
    te[0] = 1;
    while(1)
    {
        // kill (game over)
        if(damage == 1337.f){break;}
        else if(damage > 10.f)
        {
            killgame = 1;
            char title[256];
            sprintf(title, "Online Fractal Attack | %u/%u | 100%% | %.2f mins | GAME END", hits, popped, (time(0)-sepoch)/60.0);
            glfwSetWindowTitle(window, title);
            printf("sendThread: exiting, game over.\n");
            break;
        }

        // every 10ms
        usleep(cst);

        // send position
        cst = 0;
        const uint64_t mt = microtime();
        memcpy(&packet[9], &mt, sizeof(uint64_t));
        memcpy(&packet[17], &ppr, poslen);
        memcpy(&packet[29], &pp, poslen);
        uint retry = 0;
        while(sendto(ssock, packet, packet_len, 0, (struct sockaddr*)&server, sizeof(server)) < 0)
        {
            usleep(rst);
            retry++;
            cst += rst;
            if(retry >= retries)
            {
                printf("sendThread: sendto() failed after %u retries.\n", retry);
                printf("ERROR: %s\n", strerror(errno));
                break;
            }
        }
        if(cst == 0 || cst > st){cst = st;}else{cst = st - (rst*retry);}
    }
}

void *recvThread(void *arg)
{
    uint slen = sizeof(server);
    unsigned char buff[RECV_BUFF_SIZE];
    te[1] = 1;
    while(1)
    {
        // kill (game over)
        if(damage == 1337.f){break;}
        else if(damage > 10.f)
        {
            killgame = 1;
            char title[256];
            sprintf(title, "Online Fractal Attack | %u/%u | 100%% | %.2f mins | GAME END", hits, popped, (time(0)-sepoch)/60.0);
            glfwSetWindowTitle(window, title);
            printf("recvThread: exiting, game over.\n");
            break;
        }

        // wait for packet
        memset(buff, 0x00, sizeof(buff));
        const ssize_t rs = recvfrom(ssock, buff, RECV_BUFF_SIZE-1, 0, (struct sockaddr *)&server, &slen);
        if(rs < 9){continue;} // must have pid + id minimum

        if(buff[8] == MSG_TYPE_ASTEROID_POS) // asteroid pos
        {
            static const size_t ps = sizeof(uint16_t)+(sizeof(float)*8);
            size_t ofs = sizeof(uint64_t)+sizeof(uint8_t);
            uint64_t timestamp = 0;
            if(rs > sizeof(uint64_t)+sizeof(uint8_t)+sizeof(uint64_t))
            {
                memcpy(&timestamp, &buff[ofs], sizeof(uint64_t));
                ofs += sizeof(uint64_t);
#ifdef IGNORE_OLD_PACKETS
                if(timestamp >= latest_packet) // skip this packet if it is not new
                    latest_packet = timestamp;
                else
                    continue;
#endif
            }
            while(ofs+ps <= rs)
            {
                uint16_t id;
                memcpy(&id, &buff[ofs], sizeof(uint16_t));
                if(comets[id].is_boom > 0.f) // nonono ! im animating!
                {
                    ofs += ps;
                    continue;
                }
                ofs += sizeof(uint16_t);
                memcpy(&comets[id].vel, &buff[ofs], sizeof(float)*3);
                ofs += sizeof(float)*3;
                memcpy(&comets[id].pos, &buff[ofs], sizeof(float)*3);

                // timestamp correction
                const f32 s = microtimeToScalar(timestamp);
                vec c = comets[id].vel;
                vMulS(&c, c, s);
                vAdd(&comets[id].pos, comets[id].pos, c);

                ofs += sizeof(float)*3;
                memcpy(&comets[id].rot, &buff[ofs], sizeof(float));
                ofs += sizeof(float);
                memcpy(&comets[id].scale, &buff[ofs], sizeof(float));
                ofs += sizeof(float);
#ifdef VERBOSE
                printf("A[%u]: %+.2f, %+.2f, %+.2f\n", id, comets[id].pos.x, comets[id].pos.y, comets[id].pos.z);
#endif
            }
        }
        else if(buff[8] == MSG_TYPE_PLAYER_POS) // player pos
        {
            static const size_t ps = sizeof(uint8_t)+(sizeof(float)*6);
            size_t ofs = sizeof(uint64_t)+sizeof(uint8_t);
            uint64_t timestamp = 0;
            if(rs > sizeof(uint64_t)+sizeof(uint8_t)+sizeof(uint64_t))
            {
                memcpy(&timestamp, &buff[ofs], sizeof(uint64_t));
                ofs += sizeof(uint64_t);
#ifdef IGNORE_OLD_PACKETS
                if(timestamp >= latest_packet) // skip this packet if it is not new
                    latest_packet = timestamp;
                else
                    continue;
#endif
            }
            while(ofs+ps <= rs)
            {
                uint8_t id;
                memcpy(&id, &buff[ofs], sizeof(uint8_t));
                if(id == myid) // nonono ! that's me!
                {
                    ofs += ps;
                    continue;
                }
                uint16_t ido = id*3;
                ofs += sizeof(uint8_t);
                memcpy(&players[ido], &buff[ofs], sizeof(float)*3);
                ofs += sizeof(float)*3;
                memcpy(&pvel[ido], &buff[ofs], sizeof(float)*3);
                ofs += sizeof(float)*3;

                // timestamp correction (let's see how this works out)
                const f32 s = microtimeToScalar(timestamp);
                vec p = (vec){players[ido], players[ido+1], players[ido+2]};
                vec v = (vec){pvel[ido], pvel[ido+1], pvel[ido+2]};
                vMulS(&v, v, s);
                vAdd(&p, p, v);
                players[ido]   = p.x;
                players[ido+1] = p.y;
                players[ido+2] = p.z;

                //if(id == 1){printf("%lu: %+.2f, %+.2f, %+.2f / %+.2f, %+.2f, %+.2f\n", timestamp, players[ido], players[ido+1], players[ido+2], pvel[ido], pvel[ido+1], pvel[ido+2]);}

#ifdef VERBOSE
                printf("P[%u]: %+.2f, %+.2f, %+.2f\n", id, players[ido], players[ido+1], players[ido+2]);
#endif
            }
        }
        else if(buff[8] == MSG_TYPE_SUN_POS) // sun pos
        {
            static const size_t ps = sizeof(uint64_t)+sizeof(uint8_t)+sizeof(uint64_t)+sizeof(float);
            if(rs == ps)
            {
                uint64_t timestamp = 0;
                memcpy(&timestamp, &buff[sizeof(uint64_t)+sizeof(uint8_t)], sizeof(uint64_t));
#ifdef IGNORE_OLD_PACKETS
                if(timestamp >= latest_packet) // skip this packet if it is not new
                    latest_packet = timestamp;
                else
                    continue;
#endif
                memcpy(&sun_pos, &buff[sizeof(uint64_t)+sizeof(uint8_t)+sizeof(uint64_t)], sizeof(float));
                // timestamp correction
                const f32 s = microtimeToScalar(timestamp);
                sun_pos += 0.03f*s;
            }
#ifdef VERBOSE
            printf("S: %+.2f\n", sun_pos);
#endif
        }
        else if(buff[8] == MSG_TYPE_ASTEROID_DESTROYED) // asteroid destroyed **RELIABLE**
        {
            static const size_t ps = sizeof(uint64_t)+sizeof(uint8_t)+sizeof(uint64_t)+sizeof(uint16_t)+(sizeof(float)*8);
            size_t ofs = sizeof(uint64_t)+sizeof(uint8_t);
            if(rs == ps)
            {
                // read packet
                uint64_t timestamp = 0;
                memcpy(&timestamp, &buff[ofs], sizeof(uint64_t));
#ifdef IGNORE_OLD_PACKETS
                if(timestamp > latest_packet) // skip this packet if it is not new
                    latest_packet = timestamp;
                else
                    continue;
#endif
                ofs += sizeof(uint64_t);
                uint16_t id;
                memcpy(&id, &buff[ofs], sizeof(uint16_t));
                if(comets[id].is_boom > 0.f){continue;} // nonono ! im animating!
                ofs += sizeof(uint16_t);
                memcpy(&comets[id].newvel, &buff[ofs], sizeof(float)*3);
                ofs += sizeof(float)*3;
                memcpy(&comets[id].newpos, &buff[ofs], sizeof(float)*3);

                // timestamp correction
                const f32 s = microtimeToScalar(timestamp);
                vec c = comets[id].newvel;
                vMulS(&c, c, s);
                vAdd(&comets[id].newpos, comets[id].newpos, c);

                ofs += sizeof(float)*3;
                memcpy(&comets[id].newrot, &buff[ofs], sizeof(float));
                ofs += sizeof(float);
                memcpy(&comets[id].newscale, &buff[ofs], sizeof(float));

                // set asteroid to boom state
#ifdef VERBOSE
                printf("[%u] asteroid_destroyed received.\n", id);
#endif
                if(comets[id].is_boom == 0.f)
                {
                    popped++;
                    char title[256];
                    sprintf(title, "Online Fractal Attack | %u/%u | %.2f%% | %.2f mins", hits, popped, 100.f*damage, (time(0)-sepoch)/60.0);
                    glfwSetWindowTitle(window, title);
                }
                comets[id].is_boom = 1.f;

                // send confirmation
                unsigned char packet[11];
                memcpy(&packet, &pid, sizeof(uint64_t));
                memcpy(&packet[8], &(uint8_t){MSG_TYPE_ASTEROID_DESTROYED_RECVD}, sizeof(uint8_t));
                memcpy(&packet[9], &id, sizeof(uint16_t));
                csend(packet, 11);
            }
        }
        else if(buff[8] == MSG_TYPE_EXO_HIT) // exo hit **RELIABLE**
        {
            static const size_t ps = sizeof(uint64_t)+sizeof(uint8_t)+sizeof(uint64_t)+sizeof(uint16_t)+sizeof(uint16_t)+(sizeof(float)*16)+sizeof(float);
            size_t ofs = sizeof(uint64_t)+sizeof(uint8_t);
            if(rs == ps)
            {
                // read packet
                uint64_t timestamp = 0;
                memcpy(&timestamp, &buff[ofs], sizeof(uint64_t));
#ifdef IGNORE_OLD_PACKETS
                if(timestamp >= latest_packet) // skip this packet if it is not new
                    latest_packet = timestamp;
                else
                    continue;
#endif
                ofs += sizeof(uint64_t);
                uint16_t hit_id, asteroid_id;
                memcpy(&hit_id, &buff[ofs], sizeof(uint16_t));
                ofs += sizeof(uint16_t);
                memcpy(&asteroid_id, &buff[ofs], sizeof(uint16_t));
                if(comets[asteroid_id].is_boom > 0.f){continue;} // nonono ! im animating!
                ofs += sizeof(uint16_t);
                memcpy(&comets[asteroid_id].vel, &buff[ofs], sizeof(float)*3);
                ofs += sizeof(float)*3;
                memcpy(&comets[asteroid_id].pos, &buff[ofs], sizeof(float)*3);
                ofs += sizeof(float)*3;
                memcpy(&comets[asteroid_id].newvel, &buff[ofs], sizeof(float)*3);
                ofs += sizeof(float)*3;
                memcpy(&comets[asteroid_id].newpos, &buff[ofs], sizeof(float)*3);

                // timestamp correction
                const f32 s = microtimeToScalar(timestamp);
                vec c = comets[asteroid_id].newvel;
                vMulS(&c, c, s);
                vAdd(&comets[asteroid_id].newpos, comets[asteroid_id].newpos, c);

                ofs += sizeof(float)*3;
                memcpy(&comets[asteroid_id].rot, &buff[ofs], sizeof(float));
                ofs += sizeof(float);
                memcpy(&comets[asteroid_id].scale, &buff[ofs], sizeof(float));
                ofs += sizeof(float);
                memcpy(&comets[asteroid_id].newrot, &buff[ofs], sizeof(float));
                ofs += sizeof(float);
                memcpy(&comets[asteroid_id].newscale, &buff[ofs], sizeof(float));
                ofs += sizeof(float);

                // kill game?
                memcpy(&damage, &buff[ofs], sizeof(float));

#ifdef VERBOSE
                if(damage > 10.f)
                {
                    printf("[%u] exo_hit [END GAME]\n", asteroid_id);
                }
                else
                {
                    printf("[%u] exo_hit\n", asteroid_id);
                }
#endif

                // do impact
                incrementHits();

                // set asteroid to boom state
                vec dir = comets[asteroid_id].vel;
                vNorm(&dir);
                vec fwd;
                vMulS(&fwd, dir, 0.03f);
                vAdd(&comets[asteroid_id].pos, comets[asteroid_id].pos, fwd);
                comets[asteroid_id].is_boom = 1.f;

                // send confirmation
                unsigned char packet[11];
                memcpy(&packet, &pid, sizeof(uint64_t));
                memcpy(&packet[8], &(uint8_t){MSG_TYPE_EXO_HIT_RECVD}, sizeof(uint8_t));
                memcpy(&packet[9], &hit_id, sizeof(uint16_t));
                csend(packet, 11);
            }
        }
        else if(buff[8] == MSG_TYPE_PLAYER_DISCONNECTED) // player disconnect
        {
            static const size_t ps = sizeof(uint64_t)+sizeof(uint8_t)+sizeof(uint8_t);
            if(rs == ps)
            {
                // read packet
                uint8_t id;
                memcpy(&id, &buff[sizeof(uint64_t)+sizeof(uint8_t)], sizeof(uint8_t));
                uint16_t ido = id*3;
                players[ido]   = 0.f;
                players[ido+1] = 0.f;
                players[ido+2] = 0.f;

#ifdef VERBOSE
                printf("[%u] player_disconnected\n", id);
#endif

                // send confirmation
                unsigned char packet[10];
                memcpy(&packet, &pid, sizeof(uint64_t));
                memcpy(&packet[8], &(uint8_t){MSG_TYPE_PLAYER_DISCONNECTED_RECVD}, sizeof(uint8_t));
                memcpy(&packet[9], &id, sizeof(uint8_t));
                csend(packet, 10);
            }
        }
        else if(buff[8] == MSG_TYPE_REGISTER_ACCEPTED) // registration accepted
        {
            static uint accepted = 0;
            if(accepted == 0)
            {
                static const size_t ps = sizeof(uint64_t)+sizeof(uint8_t)+sizeof(uint8_t);
                if(rs == ps){memcpy(&myid, &buff[sizeof(uint64_t)+sizeof(uint8_t)], sizeof(uint8_t));}
                printf("recvThread: Registration was accepted; id(%u).\n", myid);
                accepted = 1;
            }
        }
        else if(buff[8] == MSG_TYPE_BAD_REGISTER_VALUE) // epoch too old
        {
            printf("recvThread: Your epoch already expired, it's too old slow poke!\n");
            exit(EXIT_FAILURE);
        }
    }
}

int initNet()
{
    // resolve host
    sip = HOSTtoIPv4(server_host);
    if(sip <= 0)
    {
        printf("HOSTtoIPv4() failed.\n");
        return -1;
    }

    // create socket to send to server on
    if((ssock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        printf("socket() failed.");
        printf("ERROR: %s\n", strerror(errno));
        return -2;
    }

    // create server sockaddr_in
    memset((char*)&server, 0, sizeof(server));
    server.sin_family = AF_INET; // IPv4
    server.sin_addr.s_addr = sip;
    server.sin_port = htons(UDP_PORT); // remote port

    // connect to server
    if(connect(ssock, (struct sockaddr*)&server, sizeof(server)) < 0)
    {
        printf("connect() failed.\n");
        printf("ERROR: %s\n", strerror(errno));
        return -3;
    }

    // register game (send 33 times in ~3 seconds)
    const size_t packet_len = 19;
    unsigned char packet[packet_len];
    memcpy(&packet, &pid, sizeof(uint64_t));
    memcpy(&packet[8], &(uint8_t){MSG_TYPE_REGISTER}, sizeof(uint8_t));
    memcpy(&packet[9], &sepoch, sizeof(uint64_t)); // 8 bytes
    memcpy(&packet[17], &NUM_COMETS, sizeof(uint16_t));
    uint rrg = 0, rrgf = 0;
    while(1)
    {
        if(sendto(ssock, packet, packet_len, 0, (struct sockaddr*)&server, sizeof(server)) < 0)
            rrgf++;
        else
            rrg++;
        if(rrgf+rrg >= 33){break;}
        usleep(100000); // 100ms
    }
    if(rrg == 0)
    {
        printf("After %u attempts to register with the server, none dispatched.\n", rrgf);
        printf("ERROR: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // create recv thread
    if(pthread_create(&tid[0], NULL, recvThread, NULL) != 0)
    {
        pthread_detach(tid[0]);
        printf("pthread_create(recvThread) failed.\n");
        return -4;
    }

    // create send thread
    if(pthread_create(&tid[1], NULL, sendThread, NULL) != 0)
    {
        pthread_detach(tid[1]);
        printf("pthread_create(sendThread) failed.\n");
        return -5;
    }

    // wait for launch confirmation
    while(te[0] == 0 || te[1] == 0)
        sleep(1);

    // done
    printf("Threads launched and game registration sent.\n");
    return 0;
}


//*************************************
// update & render
//*************************************
void main_loop()
{
//*************************************
// time delta for frame interpolation
//*************************************
    static double lt = 0;
    if(lt == 0){lt = t;}
    dt = t-lt;
    lt = t;

//*************************************
// keystates
//*************************************

    vec vecview[3] = {
        {view.m[0][0], view.m[1][0], view.m[2][0]}, // r
        {view.m[0][1], view.m[1][1], view.m[2][1]}, // u
        {view.m[0][2], view.m[1][2], view.m[2][2]}  // f
    };

    static f32 zrot = 0.f;

    if(keystate[2] == 1) // W
    {
        vec vdc = (vec){view.m[0][2], view.m[1][2], view.m[2][2]};
        vec m;
        vMulS(&m, vdc, MOVE_SPEED * dt);
        vAdd(&pp, pp, m);
    }
    else if(keystate[3] == 1) // S
    {
        vec vdc = (vec){view.m[0][2], view.m[1][2], view.m[2][2]};
        vec m;
        vMulS(&m, vdc, MOVE_SPEED * dt);
        vSub(&pp, pp, m);
    }

    if(keystate[0] == 1) // A
    {
        vec vdc = (vec){view.m[0][0], view.m[1][0], view.m[2][0]};
        vec m;
        vMulS(&m, vdc, MOVE_SPEED * dt);
        vAdd(&pp, pp, m);
    }
    else if(keystate[1] == 1) // D
    {
        vec vdc = (vec){view.m[0][0], view.m[1][0], view.m[2][0]};
        vec m;
        vMulS(&m, vdc, MOVE_SPEED * dt);
        vSub(&pp, pp, m);
    }

    if(keystate[4] == 1) // SPACE
    {
        vec vdc = (vec){view.m[0][1], view.m[1][1], view.m[2][1]};
        vec m;
        vMulS(&m, vdc, MOVE_SPEED * dt);
        vSub(&pp, pp, m);
    }
    else if(keystate[5] == 1) // SHIFT
    {
        vec vdc = (vec){view.m[0][1], view.m[1][1], view.m[2][1]};
        vec m;
        vMulS(&m, vdc, MOVE_SPEED * dt);
        vAdd(&pp, pp, m);
    }

    if(brake == 1)
        vMulS(&pp, pp, 0.99f*(1.f-dt));

    vec ppi = pp;
    vMulS(&ppi, ppi, dt);
    vAdd(&ppr, ppr, ppi);

    const f32 pmod = vMod(ppr);
    if(autoroll == 1 && pmod < 2.f) // self-righting
    {
        vec dve = (vec){view.m[0][0], view.m[1][0], view.m[2][0]};
        vec dvp = ppr;
        vInv(&dvp);
        vNorm(&dvp);
        const f32 ta = vDot(dve, dvp);
        if(ta < -0.03f || ta > 0.03f)
        {
            // const f32 ia = ta*0.01f;
            const f32 ia = (ta*0.03f) * ( 1.f - ((pmod - 1.14f) * 1.162790656f) ); // 0.86f
            zrot -= ia*dt;
            //printf("%f %f %f\n", zrot, ta, pmod);
        }
        else
            zrot = 0.f;
    }
    else // roll inputs
    {
        if(brake == 1)
            zrot *= 0.99f*(1.f-dt);

        if(keystate[6] && !keystate[7]) {
            if(zrot > 0.f) {
                zrot += dt*sens*10.f;
            } else {
                zrot *= 1.f-(dt*0.02f);
                zrot += dt*sens*10.f;
            }
        } else if(keystate[7] && !keystate[6]) {
            if(zrot < 0.f) {
                zrot -= dt*sens*10.f;
            } else {
                zrot *= 1.f-(dt*0.02f);
                zrot -= dt*sens*10.f;
            }
        } else if(keystate[6] && keystate[7]) {
            zrot = 0.f;
        } else {
            if (zrot > 0.09f || zrot < -0.09f)
                zrot *= 1.f-(dt*0.91f);
            else if (zrot > 0.001f)
                zrot -= 0.001f*dt;
            else if (zrot < -0.001f)
                zrot += 0.001f*dt;
            else
                zrot = 0.f;
        }
    }
    if(pmod < 1.13f) // exo collision
    {
        vec n = ppr;
        vNorm(&n);
         vReflect(&pp, pp, (vec){-n.x, -n.y, -n.z}); // better if I don't normalise pp
         vMulS(&pp, pp, 0.3f);
        vMulS(&n, n, 1.13f - pmod);
        vAdd(&ppr, ppr, n);
    }

    // ufo collision
    const uint sui = ufoind * 3;
    vec ep = (vec){exo_vertices[sui], exo_vertices[sui+1], exo_vertices[sui+2]};
    vec en = ep;
    vNorm(&en);
    vec ens;
    vMulS(&ens, en, 0.15f);
    vAdd(&ep, ep, ens);
    const float dst = t - ufospt;
    if(dst <= 43.f)
    {
        const float ud = vDist(ep, (vec){-ppr.x, -ppr.y, -ppr.z});
        if(fabsf(vSum(pp)) > 0.f && ud < 0.18f)
        {
            vec rn = (vec){-ppr.x, -ppr.y, -ppr.z};
            vSub(&rn, rn, ep);
            vNorm(&rn);

            vec n = pp;
            vNorm(&n);
            vInv(&n);

            //printf("%f\n", vDot(rn, n));
            if(vDot(rn, n) < 0.f)
            {
                vReflect(&pp, pp, (vec){rn.x, rn.y, rn.z});
                vMulS(&pp, pp, 0.3f);
            }
            else
            {
                vCopy(&pp, rn);
                vMulS(&pp, pp, 1.3f); // give a kick back for shield harassment
            }

            //printf("2: %f %f %f\n", pp.x, pp.y, pp.z);
            vMulS(&n, n, 0.18f - ud);
            if(isnormal(n.x) == 1 && isnormal(n.y) == 1 && isnormal(n.z) == 1)
            {
                //printf("3: %f %f %f\n", n.x, n.y, n.z);
                vAdd(&ppr, ppr, n);
                //printf("4: %f %f %f\n", ppr.x, ppr.y, ppr.z);
            }

            ufosht = t;
        }
    }

//*************************************
// camera
//*************************************

    // mouse delta to rot
    f32 xrot = 0.f, yrot = 0.f;
    if(focus_cursor == 1)
    {
        glfwGetCursorPos(window, &x, &y);
        xrot = (ww2-x)*sens;
        yrot = (wh2-y)*sens;
        glfwSetCursorPos(window, ww2, wh2);
    }

    // Test_User angle-axis rotation
    // https://en.wikipedia.org/wiki/Axis%E2%80%93angle_representation
    // https://en.wikipedia.org/wiki/Rodrigues%27_rotation_formula
    vec tmp0, tmp1;

    // left/right
    vMulS(&tmp0, vecview[0], cosf(xrot));

    vCross(&tmp1, vecview[1], vecview[0]);
    vMulS(&tmp1, tmp1, sinf(xrot));

    vMulS(&vecview[0], vecview[1], vDot(vecview[1], vecview[0]) * (1.f - cosf(xrot)));

    vAdd(&vecview[0], vecview[0], tmp0);
    vAdd(&vecview[0], vecview[0], tmp1);

    // up/down
    vMulS(&tmp0, vecview[1], cosf(yrot));

    vCross(&tmp1, vecview[0], vecview[1]);
    vMulS(&tmp1, tmp1, sinf(yrot));

    vMulS(&vecview[1], vecview[0], vDot(vecview[0], vecview[1]) * (1.f - cosf(yrot)));

    vAdd(&vecview[1], vecview[1], tmp0);
    vAdd(&vecview[1], vecview[1], tmp1);

    vCross(&vecview[2], vecview[0], vecview[1]);
    vCross(&vecview[1], vecview[2], vecview[0]);

    // roll
    vMulS(&tmp0, vecview[0], cosf(zrot));

    vCross(&tmp1, vecview[2], vecview[0]);
    vMulS(&tmp1, tmp1, sinf(zrot));

    vMulS(&vecview[0], vecview[2], vDot(vecview[2], vecview[0]) * (1.f - cosf(zrot)));

    vAdd(&vecview[0], vecview[0], tmp0);
    vAdd(&vecview[0], vecview[0], tmp1);

    vCross(&vecview[1], vecview[2], vecview[0]);

    vNorm(&vecview[0]);
    vNorm(&vecview[1]);
    vNorm(&vecview[2]);

    view = (mat){
        vecview[0].x, vecview[1].x, vecview[2].x, view.m[0][3],
        vecview[0].y, vecview[1].y, vecview[2].y, view.m[1][3],
        vecview[0].z, vecview[1].z, vecview[2].z, view.m[2][3],
        0.f, 0.f, 0.f, view.m[3][3]
    };

    // translate
    mTranslate(&view, ppr.x, ppr.y, ppr.z);

    // sun pos
    sun_pos += 0.03f*dt;
    lightpos.x = sinf(sun_pos) * 6.3f;
    lightpos.y = cosf(sun_pos) * 6.3f;
    lightpos.z = sinf(sun_pos) * 6.3f;

//*************************************
// render
//*************************************
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ///
    
    shadeLambert2(&position_id, &projection_id, &modelview_id, &lightpos_id, &color_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);
    glUniform3f(lightpos_id, lightpos.x, lightpos.y, lightpos.z);
    glUniform1f(opacity_id, 1.f);
    
    ///

    glBindBuffer(GL_ARRAY_BUFFER, mdlExo.vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);

    glBindBuffer(GL_ARRAY_BUFFER, mdlExo.cid);
    glVertexAttribPointer(color_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(color_id);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlExo.iid);

    glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (GLfloat*) &view.m[0][0]);
    glDrawElements(GL_TRIANGLES, exo_numind, GL_UNSIGNED_INT, 0);

    /// the inner wont draw now if occluded by the exo due to depth buffer

    glBindBuffer(GL_ARRAY_BUFFER, mdlInner.vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);

    glBindBuffer(GL_ARRAY_BUFFER, mdlInner.cid);
    glVertexAttribPointer(color_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(color_id);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlExo.iid);

    glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (GLfloat*) &view.m[0][0]);
    glDrawElements(GL_TRIANGLES, exo_numind, GL_UNSIGNED_INT, 0);

    ///

    // lambert
    shadeLambert(&position_id, &projection_id, &modelview_id, &lightpos_id, &color_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);
    glUniform3f(lightpos_id, 0.f, 0.f, 0.f);
    glUniform1f(opacity_id, 1.f);
    glUniform3f(color_id, 1.f, 1.f, 0.f);

    // bind menger
    glBindBuffer(GL_ARRAY_BUFFER, mdlMenger.vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlMenger.iid);

    // "light source" dummy object
    mIdent(&model);
    mTranslate(&model, lightpos.x, lightpos.y, lightpos.z);
    mScale(&model, 3.4f, 3.4f, 3.4f);
    mMul(&modelview, &model, &view);
    glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
    glDrawElements(GL_TRIANGLES, ncube_numind, GL_UNSIGNED_INT, 0);

    ///

    // lambert4
    shadeLambert4(&position_id, &projection_id, &modelview_id, &lightpos_id, &normal_id, &texcoord_id, &sampler_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);
    glUniform3f(lightpos_id, 0.f, 0.f, 0.f);
    glUniform1f(opacity_id, 1.f);

    // comets
    int bindstate = -1;
    int cbs = -1;
    GLushort indexsize = rock8_numind;
    for(uint16_t i = 0; i < NUM_COMETS; i++)
    {
        // simulation
        if(comets[i].is_boom > 0.f) // explode
        {
            if(cbs != 0)
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, tex[12]);
                glUniform1i(sampler_id, 0);
                cbs = 0;
            }

            if(killgame == 0)
            {
                vec dtv = comets[i].newvel;
                vMulS(&dtv, dtv, dt);
                vAdd(&comets[i].newpos, comets[i].newpos, dtv);

                if(comets[i].is_boom == 1.f)
                {
                    doExoImpact((vec){comets[i].pos.x, comets[i].pos.y, comets[i].pos.z}, (comets[i].scale+(vMod(comets[i].vel)*0.1f))*1.2f);
                    comets[i].scale *= 2.0f;
                }
            
                comets[i].is_boom -= 0.3f*dt;
                comets[i].scale -= 0.03f*dt;
            }

            if(comets[i].is_boom <= 0.f || comets[i].scale <= 0.f)
            {
                comets[i].pos = comets[i].newpos;
                comets[i].vel = comets[i].newvel;
                comets[i].rot = comets[i].newrot;
                comets[i].scale = comets[i].newscale;
                comets[i].is_boom = 0.f;
                continue;
            }
            glUniform1f(opacity_id, comets[i].is_boom);
        }
        else if(comets[i].is_boom <= 0.f) // detect impacts
        {
            if(killgame == 0)
            {
                // increment position
                vec dtv = comets[i].vel;
                vMulS(&dtv, dtv, dt);
                vAdd(&comets[i].pos, comets[i].pos, dtv);

                // player impact
                const f32 cd = vDistSq((vec){-ppr.x, -ppr.y, -ppr.z}, comets[i].pos);
                const f32 cs = comets[i].scale+0.06f;
                if(cd < cs*cs)
                {
                    // tell server of collision
                    const uint64_t mt = microtime();
                    const size_t packet_len = 19;
                    unsigned char packet[packet_len];
                    memcpy(&packet, &pid, sizeof(uint64_t));
                    memcpy(&packet[8], &(uint8_t){MSG_TYPE_ASTEROID_DESTROYED}, sizeof(uint8_t));
                    memcpy(&packet[9], &mt, sizeof(uint64_t));
                    memcpy(&packet[17], &(uint16_t){i}, sizeof(uint16_t));
                    csend(packet, 19);
#ifdef VERBOSE
                    printf("[%u] asteroid_destroyed sent.\n", i);
#endif
                }
            }

            cbs = 1;
        }

        // this is a nightmare and not super efficient but urgh for this purpose its fine
        if(cbs != 0)
        {
            const uint ti = i - (12*(i/12));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex[ti]);
            glUniform1i(sampler_id, 0);
        }

        // this would be better due to sticking to painters algo
        // if I didn't have to force a new texture on every cbs != 0 anyway
        // if(cbs != 0)
        // {
        //     static int lt = -1;
        //     static const float r = 11.f / NUM_COMETS;
        //     const uint ti = (r * (float)i) + 0.5f;
        //     if(ti != lt)
        //     {
        //         glActiveTexture(GL_TEXTURE0);
        //         glBindTexture(GL_TEXTURE_2D, tex[ti]);
        //         glUniform1i(sampler_id, 0);
        //         lt = ti;
        //     }
        // }

        // translate comet
        mIdent(&model);
        mTranslate(&model, comets[i].pos.x, comets[i].pos.y, comets[i].pos.z);

        // rotate comet
        f32 mag = 0.f;
        if(killgame == 0){mag = comets[i].rot*0.01f*t;}
        if(comets[i].rot < 100.f)
            mRotY(&model, mag);
        if(comets[i].rot < 200.f)
            mRotZ(&model, mag);
        if(comets[i].rot < 300.f)
            mRotX(&model, mag);
        
        // scale comet
        mScale(&model, comets[i].scale, comets[i].scale, comets[i].scale);

        // make modelview
        mMul(&modelview, &model, &view);
        glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);

        // bind one of the 8 rock models
        uint nbs = i * rrcs;
        if(nbs > 7){nbs = 7;}
        if(nbs != bindstate)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mdlRock[nbs].vid);
            glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(position_id);

            glBindBuffer(GL_ARRAY_BUFFER, mdlRock[nbs].nid);
            glVertexAttribPointer(normal_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(normal_id);

            glBindBuffer(GL_ARRAY_BUFFER, mdlRock[nbs].tid);
            glVertexAttribPointer(texcoord_id, 2, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(texcoord_id);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlRock[nbs].iid);
            bindstate = nbs;
        }

        if(nbs == 0){indexsize = rock1_numind;}
        else if(nbs == 1){indexsize = rock2_numind;}
        else if(nbs == 2){indexsize = rock3_numind;}
        else if(nbs == 3){indexsize = rock4_numind;}
        else if(nbs == 4){indexsize = rock5_numind;}
        else if(nbs == 5){indexsize = rock6_numind;}
        else if(nbs == 6){indexsize = rock7_numind;}
        else if(nbs == 7){indexsize = rock8_numind;}

        // draw it
        if(comets[i].is_boom > 0.f)
        {
            glEnable(GL_BLEND);
            glDrawElements(GL_TRIANGLES, indexsize, GL_UNSIGNED_SHORT, 0);
            glDisable(GL_BLEND);
        }
        else
            glDrawElements(GL_TRIANGLES, indexsize, GL_UNSIGNED_SHORT, 0);
    }

    // players
    shadeLambert(&position_id, &projection_id, &modelview_id, &lightpos_id, &color_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);
    glUniform3f(lightpos_id, 0.f, 0.f, 0.f);
    glUniform1f(opacity_id, 1.f);
    glUniform3f(color_id, 0.f, 1.f, 1.f);

    glBindBuffer(GL_ARRAY_BUFFER, mdlLow.vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlLow.iid);

    for(uint i = 0; i < MAX_PLAYERS; i++)
    {
        const uint j = i*3;
        if(players[j] != 0.f || players[j+1] != 0.f || players[j+2] != 0.f)
        {
            players[j]   += pvel[j]  *dt;
            players[j+1] += pvel[j+1]*dt;
            players[j+2] += pvel[j+2]*dt;

            mIdent(&model);
            mTranslate(&model, -players[j], -players[j+1], -players[j+2]);
            mScale(&model, 0.01f, 0.01f, 0.01f);
            mMul(&modelview, &model, &view);
            glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
            glDrawElements(GL_TRIANGLES, rock1_numind, GL_UNSIGNED_SHORT, 0);
        }
    }

    // UFO
    float dopa = 1.f;
    if(dst <= 1.f)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        dopa = dst;
    }
    else if(dst >= ufonxw) // TEST
    {
        ufospt = t;
        ufoind = esRand(0, exo_numvert-1);
        ufonxw = esRandFloat(48.f, 66.f);
    }
    else if(dst >= 42.f)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        dopa = 1.f-(dst-42.f);
    }

if(dst <= 43.f)
{
    shadePhong4(&position_id, &projection_id, &modelview_id, &normalmat_id, &lightpos_id, &normal_id, &texcoord_id, &sampler_id, &specularmap_id, &normalmap_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);
    glUniform3f(lightpos_id, 0.f, 0.f, 0.f);
    glUniform1f(opacity_id, dopa);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[14]);
    glUniform1i(sampler_id, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex[15]);
    glUniform1i(specularmap_id, 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex[16]);
    glUniform1i(normalmap_id, 2);

    glBindBuffer(GL_ARRAY_BUFFER, mdlUfo.vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);

    glBindBuffer(GL_ARRAY_BUFFER, mdlUfo.nid);
    glVertexAttribPointer(normal_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(normal_id);

    glBindBuffer(GL_ARRAY_BUFFER, mdlUfo.tid);
    glVertexAttribPointer(texcoord_id, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(texcoord_id);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlUfo.iid);

    mIdent(&model);
    mLookAt(&model, ep, en);
    mMul(&modelview, &model, &view);

    mat inverted, normalmat;
    mInvert(&inverted.m[0][0], &modelview.m[0][0]);
    mTranspose(&normalmat, &inverted);
    
    glUniformMatrix4fv(normalmat_id, 1, GL_FALSE, (GLfloat*) &normalmat.m[0][0]);
    glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
    glDrawElements(GL_TRIANGLES, ufo_numind, GL_UNSIGNED_SHORT, 0);

    /// ufo lights

    shadeFullbright(&position_id, &projection_id, &modelview_id, &color_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);
    //glUniform3f(color_id, 0.f, 1.f, 1.f);
    //glUniform3f(color_id, 0.f, fabsf(sinf(t*0.3f)), fabsf(cosf(t*0.3f)));
    const float ils = 0.3f + (0.7f - ((42.f-dst)*0.016666667f));
    float ls = (42.f-dst)*0.02380952425f;
    if(ls < 0.f){ls = 0.f;}
    glUniform3f(color_id, ls, ils, ils);
    glUniform1f(opacity_id, dopa);

    glBindBuffer(GL_ARRAY_BUFFER, mdlUfoLights.vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlUfoLights.iid);

    mIdent(&model);
    mLookAt(&model, ep, en);
    mMul(&modelview, &model, &view);

    glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
    glDrawElements(GL_TRIANGLES, ufo_lights_numind, GL_UNSIGNED_BYTE, 0);

    /// ufo tri

    glUniform3f(color_id, 0.f, 1.f, 0.f);

    glBindBuffer(GL_ARRAY_BUFFER, mdlUfoTri.vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlUfoTri.iid);

    uint pi = 0;
    for(float f = 0; f < x2PI; f+=0.1747f)
    {
        if(dst <= 42.f)
        {
            if(pi > dst*0.8571428657f){glUniform3f(color_id, 1.f, 0.f, 0.f);}
        }
        else if(dst <= 43.f){glUniform3f(color_id, 0.f, 1.f, 0.f);}
        else{glUniform3f(color_id, 1.f, 0.f, 0.f);}
        mIdent(&model);
        mLookAt(&model, ep, en);
        mRotZ(&model, f);
        mMul(&modelview, &model, &view);
        glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
        glDrawElements(GL_TRIANGLES, ufo_tri_numind, GL_UNSIGNED_BYTE, 0);
        pi++;
    }

    /// ufo beam

    shadeLambert4(&position_id, &projection_id, &modelview_id, &lightpos_id, &normal_id, &texcoord_id, &sampler_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);
    glUniform3f(lightpos_id, 0.f, 0.f, 0.f);
    glUniform1f(opacity_id, dopa*0.3f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[13]);
    glUniform1i(sampler_id, 0);

    glBindBuffer(GL_ARRAY_BUFFER, mdlUfoBeam.vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);

    glBindBuffer(GL_ARRAY_BUFFER, mdlUfoBeam.nid);
    glVertexAttribPointer(normal_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(normal_id);

    glBindBuffer(GL_ARRAY_BUFFER, mdlUfoBeam.tid);
    glVertexAttribPointer(texcoord_id, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(texcoord_id);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlUfoBeam.iid);

    mIdent(&model);
    mLookAt(&model, ep, en);
    mRotZ(&model, t*0.3f);
    mMul(&modelview, &model, &view);

    glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
glEnable(GL_BLEND);
    glDrawElements(GL_TRIANGLES, beam_numind, GL_UNSIGNED_BYTE, 0);

    // ufo shield
    const f32 dsht = t-ufosht;
    if(dsht <= 1.f)
    {
        shadeLambert2(&position_id, &projection_id, &modelview_id, &lightpos_id, &color_id, &opacity_id);
        glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);
        glUniform3f(lightpos_id, 0.f, 0.f, 0.f);
        glUniform1f(opacity_id, 1.f-dsht);

        glBindBuffer(GL_ARRAY_BUFFER, mdlUfoShield.vid);
        glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(position_id);

        glBindBuffer(GL_ARRAY_BUFFER, mdlUfoShield.cid);
        glVertexAttribPointer(color_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(color_id);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlUfoShield.iid);

        mIdent(&model);
        //mLookAt(&model, ep, en); // could just translate here
        mTranslate(&model, ep.x, ep.y, ep.z);
        mMul(&modelview, &model, &view);

        glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
        glDrawElements(GL_TRIANGLES, ufo_shield_numind, GL_UNSIGNED_SHORT, 0);
    }
}
glDisable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    // swap
    glfwSwapBuffers(window);
    last_render_time = microtime();
}

//*************************************
// Input Handelling
//*************************************
void window_size_callback(GLFWwindow* window, int width, int height)
{
    winw = width;
    winh = height;

    glViewport(0, 0, winw, winh);
    aspect = (f32)winw / (f32)winh;
    ww = (double)winw;
    wh = (double)winh;
    rww = 1.0/ww;
    rwh = 1.0/wh;
    ww2 = ww/2.0;
    wh2 = wh/2.0;
    uw = (double)aspect/ww;
    uh = 1.0/wh;
    uw2 = (double)aspect/ww2;
    uh2 = 1.0/wh2;

    mIdent(&projection);
    mPerspective(&projection, 60.0f, aspect, 0.01f, FAR_DISTANCE);
    if(projection_id != -1){glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);}
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if(action == GLFW_PRESS)
    {
        if(key == GLFW_KEY_A){ keystate[0] = 1; }
        else if(key == GLFW_KEY_D){ keystate[1] = 1; }
        else if(key == GLFW_KEY_W){ keystate[2] = 1; }
        else if(key == GLFW_KEY_S){ keystate[3] = 1; }
        else if(key == GLFW_KEY_SPACE){ keystate[4] = 1; }
        else if(key == GLFW_KEY_LEFT_SHIFT){ keystate[5] = 1; }
        else if(key == GLFW_KEY_Q){ keystate[6] = 1; }
        else if(key == GLFW_KEY_E){ keystate[7] = 1; }
        else if(key == GLFW_KEY_LEFT_CONTROL){ brake = 1; }
        else if(key == GLFW_KEY_F)
        {
            if(t-lfct > 2.0)
            {
                char strts[16];
                timestamp(&strts[0]);
                printf("[%s] FPS: %g\n", strts, fc/(t-lfct));
                lfct = t;
                fc = 0;
            }
        }
        else if(key == GLFW_KEY_R)
        {
            autoroll = 1 - autoroll;
            printf("autoroll: %u\n", autoroll);
        }
        else if(key == GLFW_KEY_ESCAPE)
        {
            focus_cursor = 1 - focus_cursor;
            if(focus_cursor == 0)
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            else
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
            window_size_callback(window, winw, winh);
            glfwSetCursorPos(window, ww2, wh2);
            glfwGetCursorPos(window, &ww2, &wh2);
        }
        else if(key == GLFW_KEY_V)
        {
            // view inversion
            printf(":: %+.2f\n", view.m[1][1]);

            // dump view matrix
            printf("%+.2f %+.2f %+.2f %+.2f\n", view.m[0][0], view.m[0][1], view.m[0][2], view.m[0][3]);
            printf("%+.2f %+.2f %+.2f %+.2f\n", view.m[1][0], view.m[1][1], view.m[1][2], view.m[1][3]);
            printf("%+.2f %+.2f %+.2f %+.2f\n", view.m[2][0], view.m[2][1], view.m[2][2], view.m[2][3]);
            printf("%+.2f %+.2f %+.2f %+.2f\n", view.m[3][0], view.m[3][1], view.m[3][2], view.m[3][3]);
            printf("---\n");
        }
    }
    else if(action == GLFW_RELEASE)
    {
        if(key == GLFW_KEY_A){ keystate[0] = 0; }
        else if(key == GLFW_KEY_D){ keystate[1] = 0; }
        else if(key == GLFW_KEY_W){ keystate[2] = 0; }
        else if(key == GLFW_KEY_S){ keystate[3] = 0; }
        else if(key == GLFW_KEY_SPACE){ keystate[4] = 0; }
        else if(key == GLFW_KEY_LEFT_SHIFT){ keystate[5] = 0; }
        else if(key == GLFW_KEY_Q){ keystate[6] = 0; }
        else if(key == GLFW_KEY_E){ keystate[7] = 0; }
        else if(key == GLFW_KEY_LEFT_CONTROL){ brake = 0; }
    }
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if(action == GLFW_PRESS)
    {
        if(button == GLFW_MOUSE_BUTTON_LEFT)
        {
            focus_cursor = 1 - focus_cursor;
            if(focus_cursor == 0)
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            else
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
            window_size_callback(window, winw, winh);
            glfwSetCursorPos(window, ww2, wh2);
            glfwGetCursorPos(window, &ww2, &wh2);
        }
        else if(button == GLFW_MOUSE_BUTTON_RIGHT)
            brake = 1;
    }
    else if(action == GLFW_RELEASE)
        brake = 0;
}

//*************************************
// Process Entry Point
//*************************************
#ifdef DEBUG_GL
void GLAPIENTRY
DebugCallback(  GLenum source, GLenum type, GLuint id,
                GLenum severity, GLsizei length,
                const GLchar *msg, const void *data)
{
    // https://gist.github.com/liam-middlebrook/c52b069e4be2d87a6d2f
    // https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDebugMessageControl.xhtml
    // https://registry.khronos.org/OpenGL-Refpages/es3/html/glDebugMessageControl.xhtml
    char* _source;
    char* _type;
    char* _severity;

    switch (source) {
        case GL_DEBUG_SOURCE_API:
        _source = "API";
        break;

        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        _source = "WINDOW SYSTEM";
        break;

        case GL_DEBUG_SOURCE_SHADER_COMPILER:
        _source = "SHADER COMPILER";
        break;

        case GL_DEBUG_SOURCE_THIRD_PARTY:
        _source = "THIRD PARTY";
        break;

        case GL_DEBUG_SOURCE_APPLICATION:
        _source = "APPLICATION";
        break;

        case GL_DEBUG_SOURCE_OTHER:
        _source = "UNKNOWN";
        break;

        default:
        _source = "UNKNOWN";
        break;
    }

    switch (type) {
        case GL_DEBUG_TYPE_ERROR:
        _type = "ERROR";
        break;

        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        _type = "DEPRECATED BEHAVIOR";
        break;

        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        _type = "UDEFINED BEHAVIOR";
        break;

        case GL_DEBUG_TYPE_PORTABILITY:
        _type = "PORTABILITY";
        break;

        case GL_DEBUG_TYPE_PERFORMANCE:
        _type = "PERFORMANCE";
        break;

        case GL_DEBUG_TYPE_OTHER:
        _type = "OTHER";
        break;

        case GL_DEBUG_TYPE_MARKER:
        _type = "MARKER";
        break;

        default:
        _type = "UNKNOWN";
        break;
    }

    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
        _severity = "HIGH";
        break;

        case GL_DEBUG_SEVERITY_MEDIUM:
        _severity = "MEDIUM";
        break;

        case GL_DEBUG_SEVERITY_LOW:
        _severity = "LOW";
        break;

        case GL_DEBUG_SEVERITY_NOTIFICATION:
        _severity = "NOTIFICATION";
        break;

        default:
        _severity = "UNKNOWN";
        break;
    }

    printf("%d: %s of %s severity, raised from %s: %s\n",
            id, _type, _severity, _source, msg);
}
#endif

int main(int argc, char** argv)
{
    sepoch = time(0);
    sepoch = ((((time_t)(((double)sepoch / 60.0) / 3.0)+1)*3)*60); // next 20th of an hour

    // help
    printf("----\n");
    printf("Online Fractal Attack\n");
    printf("----\n");
    printf("James William Fletcher (github.com/mrbid)\n");
    printf("----\n");
    printf("Argv(2): start epoch, server host/ip, num asteroids, msaa 0-16, autoroll 0-1, center window 0-1\n");
    printf("F = FPS to console.\n");
    printf("R = Toggle auto-tilt/roll around planet.\n");
    printf("W, A, S, D, Q, E, SPACE, LEFT SHIFT\n");
    printf("L-CTRL / Right Click to Brake\n");
    printf("Escape / Left Click to free mouse focus.\n");
    printf("----\n");
    printf("current epoch: %lu\n", time(0));

    // start at epoch
    if(argc >= 2)
    {
        sepoch = atoll(argv[1]);
        if(sepoch != 0 && sepoch-time(0) < 3)
        {
            printf("suggested epoch: %lu\n----\nYour epoch should be at minimum 3 seconds into the future.\n", time(0)+26);
            return 0;
        }
    }

    printf("start epoch:   %lu\n", sepoch);
    printf("----\n");

    // allow custom host
    sprintf(server_host, "vfcash.co.uk");
    if(argc >= 3){sprintf(server_host, "%s", argv[2]);}

    // custom number of asteroids (for epoch creator only)
    if(argc >= 4){NUM_COMETS = atoi(argv[3]);}
    if(NUM_COMETS > MAX_COMETS){NUM_COMETS = MAX_COMETS;}
    else if(NUM_COMETS < 16){NUM_COMETS = 16;}
    rcs = NUM_COMETS / 8;
    rrcs = 1.f / (f32)rcs;

    // allow custom msaa level
    int msaa = 16;
    if(argc >= 5){msaa = atoi(argv[4]);}

    // is auto-roll enabled
    if(argc >= 6){autoroll = atoi(argv[5]);}

    // is centered
    uint center = 1;
    if(argc >= 7){center = atoi(argv[6]);}
    
    printf("Asteroids: %hu\n", NUM_COMETS);
    printf("MSAA: %u\n", msaa);
    printf("Auto-Roll: %u\n", autoroll);
    printf("----\n");
    printf("Server Host/IP: %s\n", server_host);
    printf("----\n");

    // init glfw
    if(!glfwInit()){printf("glfwInit() failed.\n"); exit(EXIT_FAILURE);}
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SAMPLES, msaa);
    window = glfwCreateWindow(winw, winh, "Fractal Attack", NULL, NULL);
    if(!window)
    {
        printf("glfwCreateWindow() failed.\n");
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    const GLFWvidmode* desktop = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if(center == 1){glfwSetWindowPos(window, (desktop->width/2)-(winw/2), (desktop->height/2)-(winh/2));} // center window on desktop
    glfwSetWindowSizeCallback(window, window_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwMakeContextCurrent(window);
    gladLoadGL(glfwGetProcAddress);
    glfwSwapInterval(1); // 0 for immediate updates, 1 for updates synchronized with the vertical retrace, -1 for adaptive vsync

    // set icon
    glfwSetWindowIcon(window, 1, &(GLFWimage){16, 16, (unsigned char*)&icon_image.pixel_data});

    // debug
#ifdef DEBUG_GL
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(DebugCallback, 0);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
#endif

//*************************************
// bind vertex and index buffers
//*************************************

    // ***** BIND MENGER *****
    scaleBuffer(ncube_vertices, ncube_numvert*3);
    esBind(GL_ARRAY_BUFFER, &mdlMenger.vid, ncube_vertices, ncube_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ELEMENT_ARRAY_BUFFER, &mdlMenger.iid, ncube_indices, ncube_indices_size, GL_STATIC_DRAW);

    // ***** BIND INNER *****
    scaleBuffer(exo_vertices, exo_numvert*3);
    esBind(GL_ARRAY_BUFFER, &mdlInner.vid, exo_vertices, exo_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlInner.cid, inner_colors, inner_colors_size, GL_STATIC_DRAW);

    // ***** BIND EXO *****
    GLsizeiptr s = exo_numvert*3;
    for(GLsizeiptr i = 0; i < s; i+=3)
    {
        const f32 g = (exo_colors[i] + exo_colors[i+1] + exo_colors[i+2]) / 3;
        const f32 h = (1.f-g)*0.01f;
        vec v = {exo_vertices[i], exo_vertices[i+1], exo_vertices[i+2]};
        vNorm(&v);
        vMulS(&v, v, h);
        exo_vertices[i]   -= v.x;
        exo_vertices[i+1] -= v.y;
        exo_vertices[i+2] -= v.z;
        exo_vertices[i]   *= 1.03f;
        exo_vertices[i+1] *= 1.03f;
        exo_vertices[i+2] *= 1.03f;
    }
    esBind(GL_ARRAY_BUFFER, &mdlExo.vid, exo_vertices, exo_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlExo.cid, exo_colors, exo_colors_size, GL_STATIC_DRAW);
    esBind(GL_ELEMENT_ARRAY_BUFFER, &mdlExo.iid, exo_indices, exo_indices_size, GL_STATIC_DRAW);

    // ***** BIND ROCK1 *****
    esBind(GL_ARRAY_BUFFER, &mdlRock[0].vid, rock1_vertices, rock1_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[0].nid, rock1_normals, rock1_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[0].iid, rock1_indices, rock1_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[0].tid, rock1_uvmap, rock1_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND ROCK2 *****
    esBind(GL_ARRAY_BUFFER, &mdlRock[1].vid, rock2_vertices, rock2_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[1].nid, rock2_normals, rock2_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[1].iid, rock2_indices, rock2_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[1].tid, rock2_uvmap, rock2_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND ROCK3 *****
    esBind(GL_ARRAY_BUFFER, &mdlRock[2].vid, rock3_vertices, rock3_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[2].nid, rock3_normals, rock3_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[2].iid, rock3_indices, rock3_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[2].tid, rock3_uvmap, rock3_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND ROCK4 *****
    esBind(GL_ARRAY_BUFFER, &mdlRock[3].vid, rock4_vertices, rock4_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[3].nid, rock4_normals, rock4_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[3].iid, rock4_indices, rock4_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[3].tid, rock4_uvmap, rock4_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND ROCK5 *****
    esBind(GL_ARRAY_BUFFER, &mdlRock[4].vid, rock5_vertices, rock5_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[4].nid, rock5_normals, rock5_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[4].iid, rock5_indices, rock5_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[4].tid, rock5_uvmap, rock5_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND ROCK6 *****
    esBind(GL_ARRAY_BUFFER, &mdlRock[5].vid, rock6_vertices, rock6_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[5].nid, rock6_normals, rock6_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[5].iid, rock6_indices, rock6_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[5].tid, rock6_uvmap, rock6_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND ROCK7 *****
    esBind(GL_ARRAY_BUFFER, &mdlRock[6].vid, rock7_vertices, rock7_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[6].nid, rock7_normals, rock7_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[6].iid, rock7_indices, rock7_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[6].tid, rock7_uvmap, rock7_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND ROCK8 *****
    esBind(GL_ARRAY_BUFFER, &mdlRock[7].vid, rock8_vertices, rock8_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[7].nid, rock8_normals, rock8_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[7].iid, rock8_indices, rock8_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlRock[7].tid, rock8_uvmap, rock8_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND LOW *****
    esBind(GL_ARRAY_BUFFER, &mdlLow.vid, low_vertices, low_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ELEMENT_ARRAY_BUFFER, &mdlLow.iid, low_indices, low_indices_size, GL_STATIC_DRAW);

    // ***** BIND UFO *****
    scaleBuffer(ufo_vertices, ufo_numvert*3);
    esBind(GL_ARRAY_BUFFER, &mdlUfo.vid, ufo_vertices, ufo_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlUfo.nid, ufo_normals, ufo_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlUfo.iid, ufo_indices, ufo_indices_size, GL_STATIC_DRAW);
    flipUV(ufo_uvmap, ufo_numvert*2);
    esBind(GL_ARRAY_BUFFER, &mdlUfo.tid, ufo_uvmap, ufo_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND UFO DAMAGED *****
    // scaleBuffer(ufo_damaged_vertices, ufo_damaged_numvert*3);
    // esBind(GL_ARRAY_BUFFER, &mdlUfoDamaged.vid, ufo_damaged_vertices, ufo_damaged_vertices_size, GL_STATIC_DRAW);
    // esBind(GL_ARRAY_BUFFER, &mdlUfoDamaged.nid, ufo_damaged_normals, ufo_damaged_normals_size, GL_STATIC_DRAW);
    // esBind(GL_ARRAY_BUFFER, &mdlUfoDamaged.iid, ufo_damaged_indices, ufo_damaged_indices_size, GL_STATIC_DRAW);
    // flipUV(ufo_damaged_uvmap, ufo_damaged_numvert*2);
    // esBind(GL_ARRAY_BUFFER, &mdlUfoDamaged.tid, ufo_damaged_uvmap, ufo_damaged_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND UFO BEAM *****
    scaleBuffer(beam_vertices, beam_numvert*3);
    esBind(GL_ARRAY_BUFFER, &mdlUfoBeam.vid, beam_vertices, beam_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlUfoBeam.nid, beam_normals, beam_normals_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlUfoBeam.iid, beam_indices, beam_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlUfoBeam.tid, beam_uvmap, beam_uvmap_size, GL_STATIC_DRAW);

    // ***** BIND UFO_LIGHTS *****
    scaleBuffer(ufo_lights_vertices, ufo_lights_numvert*3);
    esBind(GL_ARRAY_BUFFER, &mdlUfoLights.vid, ufo_lights_vertices, ufo_lights_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ELEMENT_ARRAY_BUFFER, &mdlUfoLights.iid, ufo_lights_indices, ufo_lights_indices_size, GL_STATIC_DRAW);

    // ***** BIND UFO_TRI *****
    scaleBuffer(ufo_tri_vertices, ufo_tri_numvert*3);
    esBind(GL_ARRAY_BUFFER, &mdlUfoTri.vid, ufo_tri_vertices, ufo_tri_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ELEMENT_ARRAY_BUFFER, &mdlUfoTri.iid, ufo_tri_indices, ufo_tri_indices_size, GL_STATIC_DRAW);

    // ***** BIND UFO SHIELD *****
    scaleBuffer(ufo_shield_vertices, ufo_shield_numvert*3);
    esBind(GL_ARRAY_BUFFER, &mdlUfoShield.vid, ufo_shield_vertices, ufo_shield_vertices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlUfoShield.iid, ufo_shield_indices, ufo_shield_indices_size, GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlUfoShield.cid, ufo_shield_colors, ufo_shield_colors_size, GL_STATIC_DRAW);

    // ***** LOAD TEXTURES *****
    tex[0] = esLoadTexture(512, 512, tex_rock1);
    tex[1] = esLoadTexture(512, 512, tex_rock2);
    tex[2] = esLoadTexture(512, 512, tex_rock3);
    tex[3] = esLoadTexture(512, 512, tex_rock4);
    tex[4] = esLoadTexture(512, 512, tex_rock5);
    tex[5] = esLoadTexture(512, 512, tex_rock6);
    tex[6] = esLoadTexture(512, 512, tex_rock7);
    tex[7] = esLoadTexture(512, 512, tex_rock8);
    tex[8] = esLoadTexture(512, 512, tex_rock9);
    tex[9] = esLoadTexture(512, 512, tex_rock10);
    tex[10] = esLoadTexture(512, 512, tex_rock11);
    tex[11] = esLoadTexture(512, 512, tex_rock12);
    tex[12] = esLoadTexture(512, 512, tex_flames);
    tex[13] = esLoadTextureWrapped(256, 256, tex_plasma);
    tex[14] = esLoadTexture(4096, 4096, tex_ufodiff);
    tex[15] = esLoadTexture(4096, 4096, tex_ufospec);
    tex[16] = esLoadTexture(4096, 4096, tex_ufonorm);

//*************************************
// compile & link shader programs
//*************************************

    makeLambert();
    makeLambert2();
    makeLambert4();
    makeFullbright();
    makePhong4();

//*************************************
// configure render options
//*************************************

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.f, 0.f, 0.f, 0.f);

//*************************************
// execute update / render loop
//*************************************

    // render loading screen
    glfwSetWindowTitle(window, "Please wait...");
    window_size_callback(window, winw, winh);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shadeLambert(&position_id, &projection_id, &modelview_id, &lightpos_id, &color_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (GLfloat*) &projection.m[0][0]);
    glUniform3f(lightpos_id, 0.f, 0.f, 0.f);
    glUniform1f(opacity_id, 1.f);
    glUniform3f(color_id, 1.f, 1.f, 0.f);
    glBindBuffer(GL_ARRAY_BUFFER, mdlMenger.vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdlMenger.iid);
    mIdent(&view);
    mTranslate(&view, 0.f, 0.f, -0.19f);
    glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &view.m[0][0]);
    glDrawElements(GL_TRIANGLES, ncube_numind, GL_UNSIGNED_INT, 0);
    glfwSwapBuffers(window);

#ifndef FAST_START
    // create net
    initNet();

    // wait until epoch
    //printf("%lu\n%lu\n-\n", (time_t)((double)microtime()*0.000001), time(0));
    while((time_t)((double)microtime()*0.000001) < sepoch)
    {
        usleep(1000); // this reduces the accuracy by the range in microseconds (1ms)
        char title[256];
        uint wp = 0;
        for(uint i = 0; i < MAX_PLAYERS; i++)
        {
            const uint j = i*3;
            if(players[j] != 0.f || players[j+1] != 0.f || players[j+2] != 0.f)
                wp++;
        }
        sprintf(title, "Please wait... %lu seconds. Total players waiting %u.", sepoch-time(0), wp+1);
        glfwSetWindowTitle(window, title);
    }
#endif
    glfwSetWindowTitle(window, "Online Fractal Attack");
    window_size_callback(window, winw, winh);

    // init
    t = glfwGetTime();
    lfct = t;
    
    // event loop
    while(!glfwWindowShouldClose(window))
    {
        t = glfwGetTime();
        glfwPollEvents();
        main_loop();
        fc++;
    }

    // done
    damage = 1337.f;
    glfwDestroyWindow(window);
    glfwTerminate();
    exit(EXIT_SUCCESS);
    return 0;
}
