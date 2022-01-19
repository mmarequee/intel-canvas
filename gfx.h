#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#define GFX_SIZEOF_MEMPOOL		262144
#define NAME_LEN				32

#define GFX_COMMAND_BASE			0x40
#define GFX_COMMAND_END				0xA0

#define GFX_IOCTL_BASE				'd'
#define GFX_IOCTL_RW(id, size)			_IOC(_IOC_READ|_IOC_WRITE, GFX_IOCTL_BASE, id, size)
#define GFX_IOCTL_R(id, size)			_IOC(_IOC_READ, GFX_IOCTL_BASE, id, size)
#define GFX_IOCTL_W(id, size)			_IOC(_IOC_WRITE, GFX_IOCTL_BASE, id, size)

#define GFX_RESOURCES				0xA0
#define GFX_SIZEOF_RESOURCES			64

#define GFX_CONNECTOR				0xA7
#define GFX_SIZEOF_CONNECTOR			76

#define GFX_ENCODER				0xA6
#define GFX_SIZEOF_ENCODER			20

#define GFX_CRTC_R				0xA1
#define GFX_CRTC_W				0xA2
#define GFX_SIZEOF_CRTC				104 

#define GFX_SIZEOF_MODE				68

#define GFX_SIZEOF_PROPERTIES			64

#define GFX_ADD_FB				0xAE
#define GFX_SIZEOF_FB_CMD			28
#define GFX_SIZEOF_PAGE_FLIP			24

#define GFX_PAGE_FLIP_FLAG			0x01
#define GFX_PAGE_FLIP				0xB0

#define GFX_EVENT_VBLANK			0x01
#define GFX_EVENT_PAGE_FLIP_DONE		0x02

typedef struct
{
	unsigned int clock;
	unsigned short hdisplay, hsync_start, hsync_end, htotal, hskew;
	unsigned short vdisplay, vsync_start, vsync_end, vtotal, vscan;
	unsigned int vrefresh;
	unsigned int flags;
	unsigned int type;
	char name[NAME_LEN];
} gfx_mode;

typedef struct
{
	unsigned int connector_id;
	unsigned int connector_type;
	unsigned int encoder_id;
	unsigned int encoder_type;
	unsigned int crtc_id;
	unsigned int num_of_modes;
	gfx_mode * modes;
} gfx_set;

int gfx_ioctl(int fd, unsigned long request, void * arg);

void gfx_get_info(int fd, void * mempool, int * mempool_offset);
void gfx_get_modes(void * mempool, void * set, unsigned int * num_of_active_connectors, unsigned int * sizeof_set);
