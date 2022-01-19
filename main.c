#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <asm/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <assert.h>

#include <pthread.h>

#include "gfx.h"
/*
#include "font.h"
*/


int	fd;
void	*gfx_info_ptr;
int	gfx_info_size;

void * gfx_current_set_ptr;
gfx_set * gfx_current_set;
unsigned int gfx_sizeof_set, active_connectors;

/* Intel specific variable setup */

#define INTEL_GEM_CREATE		0x1B
#define INTEL_GEM_CREATE_SIZE		16

#define INTEL_GEM_GET_TILING		0x22
#define INTEL_GEM_GET_TILING_SIZE	12

#define INTEL_GEM_SET_TILING		0x21
#define INTEL_GEM_SET_TILING_SIZE	16

#define INTEL_TILING_X			1
#define INTEL_TILING_Y			2
#define INTEL_TILING_NONE		0

#define INTEL_GEM_MMAP			0x1E
#define INTEL_GEM_MMAP_SIZE		32

#define INTEL_GEM_MMAP_GTT		0x24
#define INTEL_GEM_MMAP_GTT_SIZE		16


#define	INTEL_GEM_READ			0x1C
#define INTEL_GEM_READ_SIZE		32

#define	INTEL_GEM_WRITE			0x1D
#define INTEL_GEM_WRITE_SIZE		32

#define INTEL_PAGE_FLIP			0x02

#define SIZEOF_COMMAND_STREAM		1024

/**/

void * command_stream;			 __attribute__((aligned(16))) 

#define NUM_OF_FRAMEBUFFERS		2

struct
{
	unsigned int width;
	unsigned int height;
	unsigned int pitch;
	unsigned long int size;
	
} mode_info;

struct
{
	void *fb;
	unsigned int fb_id;
	unsigned int bpp;
	unsigned int depth;
	unsigned int handle;
	unsigned long int offset;
	
} framebuffer[NUM_OF_FRAMEBUFFERS];

void gfx_create_framebuffer(void * cmd_stream, void ** fb, unsigned int * fb_id_ptr, unsigned int width, unsigned int height, unsigned int * pitch, unsigned long int * size, unsigned int bpp, unsigned int depth, unsigned int * handle)
{
	*pitch = width * 4;
	*pitch = (*pitch + 128 - 1) & ~(128 - 1);			/* changes width  to a multiple of 4 */
	*size  = *pitch * ((height + 4 - 1) & ~(4 - 1));		/* changes height to a multiple of 4 */

	*(unsigned long int *)(cmd_stream) = *size;

	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_COMMAND_BASE + INTEL_GEM_CREATE, INTEL_GEM_CREATE_SIZE), cmd_stream);

	*handle = *(unsigned int *)(cmd_stream + 8); 

	memset(cmd_stream, 0, INTEL_GEM_CREATE_SIZE); 

	*(unsigned int *)(cmd_stream)		= *handle;
	*(unsigned int *)(cmd_stream + 4)	= INTEL_TILING_X;
	*(unsigned int *)(cmd_stream + 8)	= *pitch;	

	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_COMMAND_BASE + INTEL_GEM_SET_TILING, INTEL_GEM_SET_TILING_SIZE), cmd_stream);

	memset(cmd_stream, 0, INTEL_GEM_SET_TILING_SIZE);
	/*
	*(unsigned int *)(cmd_stream)		= *handle;
	*(unsigned int *)(cmd_stream + 8)	= 0;
	*(unsigned int *)(cmd_stream + 16)	= *size;
	
	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_COMMAND_BASE + INTEL_GEM_MMAP, INTEL_GEM_MMAP_SIZE), cmd_stream);

	*fb = *(unsigned int **)(cmd_stream + 24);
	*/
	*(unsigned int *)(cmd_stream) = *handle;

	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_COMMAND_BASE + INTEL_GEM_MMAP_GTT, INTEL_GEM_MMAP_GTT_SIZE), cmd_stream);

	/* *offset = *(unsigned long int *)(cmd_stream + 8);
	*/
	*fb = mmap(0, *size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, *(unsigned long int *)(cmd_stream + 8));

	memset(cmd_stream, 0, INTEL_GEM_MMAP_GTT_SIZE);

	*(unsigned int *)(cmd_stream + 4)	= width;
	*(unsigned int *)(cmd_stream + 8)	= height;
	*(unsigned int *)(cmd_stream + 12)	= *pitch;
	*(unsigned int *)(cmd_stream + 16)	= bpp;
	*(unsigned int *)(cmd_stream + 20)	= depth;
	*(unsigned int *)(cmd_stream + 24)	= *handle;
	
	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_ADD_FB, GFX_SIZEOF_FB_CMD), cmd_stream);

	*fb_id_ptr = *(unsigned int *)(cmd_stream);

	memset(cmd_stream, 0, GFX_SIZEOF_FB_CMD);
}

void gfx_set_mode(void * cmd_stream, unsigned int * connector_id_ptr, unsigned int crtc_id, unsigned int fb_id, unsigned int x, unsigned int y, unsigned int mode_valid, void * mode_info_ptr)
{
	*(unsigned int **)(cmd_stream)		= connector_id_ptr;
	*(unsigned int *)(cmd_stream + 8)	= 1; 
	*(unsigned int *)(cmd_stream + 12)	= crtc_id;
	*(unsigned int *)(cmd_stream + 16)	= fb_id;
	*(unsigned int *)(cmd_stream + 20)	= x;
	*(unsigned int *)(cmd_stream + 24)	= y;
	*(unsigned int *)(cmd_stream + 32)	= mode_valid;

	memcpy((cmd_stream + 36), mode_info_ptr, GFX_SIZEOF_MODE);

	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_CRTC_W, GFX_SIZEOF_CRTC), cmd_stream);

	memset(cmd_stream, 0, GFX_SIZEOF_CRTC);
}	

void gfx_page_flip(void * cmd_stream, unsigned int crtc_id, unsigned int fb_id, unsigned int flag, void * data)
{
	*(unsigned int *)(cmd_stream)		= crtc_id;
	*(unsigned int *)(cmd_stream + 4)	= fb_id;
	*(unsigned int *)(cmd_stream + 8)	= flag;
	*(unsigned int *)(cmd_stream + 12)	= 0;
	*(unsigned int **)(cmd_stream + 16)	= data;
	
	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_PAGE_FLIP, GFX_SIZEOF_PAGE_FLIP), cmd_stream);

	/* GFX_COMMAND_BASE + INTEL_PAGE_FLIP*/

	memset(cmd_stream, 0, GFX_SIZEOF_PAGE_FLIP);	
}

void gfx_write(int fd, void * cmd_stream, unsigned int handle, unsigned long int offset, unsigned long int size, void * buf_ptr)
{
	*(unsigned int *)(cmd_stream)		= handle;
	*(unsigned int *)(cmd_stream + 8)	= offset;
	*(unsigned int *)(cmd_stream +	16)	= size;
	*(unsigned int **)(cmd_stream + 24)	= buf_ptr;
	
	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_COMMAND_BASE + INTEL_GEM_WRITE, INTEL_GEM_WRITE_SIZE), cmd_stream);

	memset(cmd_stream, 0, INTEL_GEM_WRITE_SIZE);	
}

void gfx_read(int fd, void * cmd_stream, unsigned int handle, unsigned long int offset, unsigned long int size, void * buf_ptr)
{
	*(unsigned int *)(cmd_stream)		= handle;
	*(unsigned int *)(cmd_stream + 8)	= offset;
	*(unsigned int *)(cmd_stream +	16)	= size;
	*(unsigned int **)(cmd_stream + 24)	= buf_ptr;
	
	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_COMMAND_BASE + INTEL_GEM_READ, INTEL_GEM_READ_SIZE), cmd_stream);

	memset(cmd_stream, 0, INTEL_GEM_READ_SIZE);	
}

void gfx_clear_framebuffer(void * fb, unsigned int width, unsigned int height, unsigned int pitch)
{
	int x, y;

	for(y = 0; y < height; y++)
		for(x = 0; x < width; x++)
			*(unsigned int *)(fb + y * width + x) = 0x00000000;
}

int gfx_event(int fd, void * cmd_stream)
{
	int len = read(fd, (char *)cmd_stream, 1024); 
	int count = 0;

	while(count < len)
	{
		if(*(int *)(cmd_stream + count) == GFX_EVENT_PAGE_FLIP_DONE)
			return GFX_EVENT_PAGE_FLIP_DONE;  
		/* Fix check if memsetting command_stream to 0 is necessary */
		/* Use switch if more EVENTs */
		/* start.{sequence tv_sec u_sec}  timestamp readout is missing */ 
		count += *(int *)(cmd_stream + count + 4);
	}
	
	return(-1);
}

int i, k;
int flip = 0; 
int color;
int color_mask = 0x00000000;
unsigned int *buffer;	__attribute__((aligned(8)))

int main(void)
{
	gfx_info_ptr = malloc(GFX_SIZEOF_MEMPOOL);
	memset(gfx_info_ptr, 0, GFX_SIZEOF_MEMPOOL);

	gfx_current_set_ptr = malloc(GFX_SIZEOF_MEMPOOL);
	memset(gfx_current_set_ptr, 0, GFX_SIZEOF_MEMPOOL);

	fd = open("/dev/dri/card0", O_RDWR, 0);
	if (fd < 0) 
	{
		printf("i915 failed.\n");
		return -1;
	}
	
	gfx_get_info(fd, gfx_info_ptr, &gfx_info_size);

	gfx_get_modes(gfx_info_ptr, gfx_current_set_ptr, &active_connectors, &gfx_sizeof_set);

	gfx_current_set = (gfx_set *)realloc(gfx_current_set_ptr, gfx_sizeof_set);


	if(active_connectors)
	{	
		for(i = 0; i < active_connectors; i++)
		{
			printf("Connector ID\tConnector type\tEncoder ID\tEncoder type\tCRTC ID\t\tNumber of Modes\n");
			printf("%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\n",	
									gfx_current_set[i].connector_id, 
									gfx_current_set[i].connector_type, 
									gfx_current_set[i].encoder_id, 
									gfx_current_set[i].encoder_type, 
									gfx_current_set[i].crtc_id, 
									gfx_current_set[i].num_of_modes);
			for(k = 0; k < gfx_current_set[i].num_of_modes; k++)
				printf("ModeNo.%d %s: %dx%d at %dHz\n", k + 1,
									gfx_current_set[i].modes[k].name,
									gfx_current_set[i].modes[k].hdisplay, 
									gfx_current_set[i].modes[k].vdisplay,
									gfx_current_set[i].modes[k].vrefresh); 
		}
	} else
	{
		printf("Graphics modes not available.\n");
		exit(1);
	}

	command_stream = malloc(SIZEOF_COMMAND_STREAM); 
	memset(command_stream, 0, SIZEOF_COMMAND_STREAM);

	mode_info.width		= gfx_current_set[0].modes[0].hdisplay;
	mode_info.height	= gfx_current_set[0].modes[0].vdisplay;

	for(i = 0; i < NUM_OF_FRAMEBUFFERS; i++)
	{
		gfx_create_framebuffer(command_stream, &framebuffer[i].fb, &framebuffer[i].fb_id, mode_info.width, mode_info.height, &mode_info.pitch, &mode_info.size, 32, 24, &framebuffer[i].handle);
		gfx_clear_framebuffer(framebuffer[i].fb, mode_info.width, mode_info.height, mode_info.pitch);
	}

	/* Activate 1st FrameBuffer */
	gfx_page_flip(command_stream, gfx_current_set[0].crtc_id, framebuffer[0].fb_id, GFX_PAGE_FLIP_FLAG, NULL);
	
	gfx_set_mode(command_stream, &gfx_current_set[0].connector_id, gfx_current_set[0].crtc_id, framebuffer[0].fb_id, 0, 0, 1, &gfx_current_set[0].modes[0]);

	/* Fill the FrameBuffers with test data */

	/* static unsigned int buffer[4325376]; */

	buffer = calloc(mode_info.size >> 2, sizeof(unsigned int)); 

	for(i = 0; i < mode_info.size >> 2; i++)
		buffer[i]=0xFFFFFFFF;

	for(i = 0; i < mode_info.size >> 2; i++)
		if(buffer[i] != 0xFFFFFFFF) { printf("Fail! %d\n\n", i); break; };

	gfx_write(fd, command_stream, framebuffer[0].handle, 0, mode_info.size, buffer);

	for(i = 0; i < mode_info.size >> 2; i++)
		buffer[i]=0x00000000;

	gfx_read(fd, command_stream, framebuffer[0].handle, 0, mode_info.size, buffer);

	for(i = 0; i < mode_info.size >> 2; i++)
		if(buffer[i] != 0xFFFFFFFF) { printf("Fail! %d\n\n", i); break; };
		
	while(1)
	{
		color_mask += 0x01; if(color_mask == 0xFF) color_mask = 0;
/*
		for(i = 0; i < mode_info.size >> 2; i++)
			buffer[i]=(color_mask) + (color_mask << 8 ) + (color_mask << 16);
*/		
/*
		gfx_write(fd, command_stream, framebuffer[flip ^1].handle, 0, mode_info.size, buffer);
*/		
		if(gfx_event(fd, command_stream) == GFX_EVENT_PAGE_FLIP_DONE)
		{
			for(i = 0; i < mode_info.height; i++)
				for(k = 0; k < mode_info.width; k++)
			    {
		    		/* if(flip ^ 1) color = 0; else */ color = 0x5b88a4;  
		    		/* (0x00130502 * ((k / mode_info.width) >> 6)) + (0x000a1120 * ((k % mode_info.width) >> 6)); */ /* + (color_mask) + (color_mask << 8) + (color_mask << 16);*/
			    	*(unsigned int *)(framebuffer[flip ^ 1].fb + i * mode_info.pitch + k * 4) = color;
		    	}
/*              
			render_font(framebuffer[flip ^ 1].fb, mode_info.pitch);
*/			
			    gfx_page_flip(command_stream, gfx_current_set[0].crtc_id, framebuffer[flip ^ 1].fb_id, GFX_PAGE_FLIP_FLAG, NULL);			

/*			while(gfx_event(fd, command_stream) != GFX_EVENT_PAGE_FLIP_DONE); 
*/
			    flip ^= 1; /* if x Hz is given, should flip x times per second */
			/*	memset(framebuffer[flip ^ 1].fb, 0, mode_info.size);   */
		 }
		
	}

	free(buffer);
	/**/
	for(i = 0; i < NUM_OF_FRAMEBUFFERS; i++)
	{
		munmap(framebuffer[i].fb, mode_info.size);
	}
/*
	*(unsigned int *)(command_stream) = (unsigned long int)handle;
	
	gfx_ioctl(fd, GFX_IOCTL_RW(GFX_COMMAND_BASE + INTEL_GEM_CLOSE, INTEL_GEM_CLOSE_SIZE), command_stream);
*/	
	free(gfx_info_ptr);
	free(gfx_current_set);

	free(command_stream);
	
	return 0;
}




