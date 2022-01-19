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

#include "gfx.h"

int gfx_ioctl(int fd, unsigned long request, void * arg)
{
	int ret;

	do
	{
		ret = ioctl(fd, request, arg);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));

	return ret;
}

void gfx_mempool_alloc(void * mempool, int * mempool_offset, int num_of_units, int offset_num_of_units, unsigned short *sizeof_unit_data)
{
	int i = 0; 
	int offset;

	offset = *(unsigned int *)(mempool + offset_num_of_units + i * sizeof(unsigned int)) * sizeof_unit_data[i];

calculate_pointers:

		if(offset != 0)
			*(unsigned int **)(mempool + (i * sizeof(unsigned int *))) = mempool + *mempool_offset;
		else
			*(unsigned int **)(mempool + (i * sizeof(unsigned int *))) = NULL;
			
		*mempool_offset += offset; 

		i++;

		if(i == num_of_units) goto done;

		offset = *(unsigned int *)(mempool + offset_num_of_units + i * sizeof(unsigned int)) * sizeof_unit_data[i];
		
		goto calculate_pointers;
		
done:		return;
}

/* Ugly hack since layout of Connector Pointers doesn't correspond to layout of Connector Counters */
/* Needs a kernel patch and if to be submitted, also a drm_mode.h patch */
void gfx_mempool_connector(void * memregion, void * mempool, int * mempool_offset)
{
	*(unsigned int **)(memregion +  0) = mempool + *mempool_offset;
	*mempool_offset += *(unsigned int *)(memregion + 40) * GFX_SIZEOF_ENCODER;
	*(unsigned int **)(memregion +  8) = mempool + *mempool_offset;
	*mempool_offset += *(unsigned int *)(memregion + 32) * GFX_SIZEOF_MODE;
	*(unsigned int **)(memregion + 16) = mempool + *mempool_offset;
	*mempool_offset += *(unsigned int *)(memregion + 36) * GFX_SIZEOF_PROPERTIES;
	*(unsigned int **)(memregion + 24) = NULL; /* Leave out Property Values for now */
}

void gfx_get_region(	int			fd,
			unsigned int		* region_ptr, 
			int			num_of_regions, 
			void			* mempool, 
			int			* mempool_offset,
			int			ID,
			int			num_of_units, 
			int			offset_num_of_units, 
			unsigned short		* sizeof_unit_data,
			const unsigned char	ioctl_id, 
			const unsigned char	ioctl_size)
{
	void * memregion;
	int region = 0;
	int i = 0;
	
	do
	{
		if(region_ptr != NULL)		/* Remove check of const pointer within a loop */
			region = *(region_ptr + i);
		i++;

		memregion = mempool + *mempool_offset;
		*mempool_offset += ioctl_size;

		*(unsigned int *)(memregion + ID) = region; 

		gfx_ioctl(fd, GFX_IOCTL_RW(ioctl_id, ioctl_size), memregion);

		if(num_of_units != 0)		/* Remove check of const within a loop */
		{		
			if(ioctl_id == GFX_CONNECTOR)
				gfx_mempool_connector(memregion, mempool, mempool_offset);
			else
				gfx_mempool_alloc(memregion, mempool_offset, num_of_units, offset_num_of_units, sizeof_unit_data);
	
			gfx_ioctl(fd, GFX_IOCTL_RW(ioctl_id, ioctl_size), memregion);
		}
	} while (i < num_of_regions);

}

void gfx_get_info(int fd, void * mempool, int * mempool_offset)
{
	unsigned short sizeof_Resources_IDs[4]	= {4, 4, 4, 4};
	unsigned short sizeof_CRTC_Regions	= GFX_SIZEOF_CONNECTOR;
	
	*mempool_offset = 0;

	/* Resources */
	gfx_get_region(fd, NULL, 1, mempool, mempool_offset, 0, 4, 32, sizeof_Resources_IDs, GFX_RESOURCES, GFX_SIZEOF_RESOURCES);

	/* Framebuffers */

	/* CRTCs */
	gfx_get_region(fd, *(unsigned int **)(mempool +  8), *(unsigned int *)(mempool + 36), mempool, mempool_offset, 12, 1, 8, &sizeof_CRTC_Regions, GFX_CRTC_R, GFX_SIZEOF_CRTC);

	/* Connectors associated with CRTCs */

	/* Connectors */
	gfx_get_region(fd, *(unsigned int **)(mempool + 16), *(unsigned int *)(mempool + 40), mempool, mempool_offset, 48, 3, 0, NULL, GFX_CONNECTOR, GFX_SIZEOF_CONNECTOR);

	/* Modes */
	/* Encoders */
	/* Properties */

	/* Encoders */
	gfx_get_region(fd, *(unsigned int **)(mempool + 24), *(unsigned int *)(mempool + 44), mempool, mempool_offset, 0, 0, 0, NULL, GFX_ENCODER, GFX_SIZEOF_ENCODER);
}

void gfx_get_modes(void * mempool, void * set, unsigned int * num_of_active_connectors, unsigned int * sizeof_set)
{
	int i, k, offset; 
	int set_offset  = 0;
	int mode_offset;
	int num_of_connectors = *(unsigned int *)(mempool + 40);
	unsigned int mode_offsets[num_of_connectors];
	unsigned int mode_sizes  [num_of_connectors];

	*num_of_active_connectors = 0;

	offset = GFX_SIZEOF_RESOURCES;

	/* Skip Resources and IDs */
	
	for(i = 0; i < 16; i += 4)
		offset += *(unsigned int *)(mempool + 32 + i) * sizeof(int);

	/* Skip CRTCs */

	for(i = 0; i < *(unsigned int *)(mempool + 36); i++)		
		offset += GFX_SIZEOF_CRTC + *(unsigned int *)(mempool + offset + 8) * GFX_SIZEOF_CONNECTOR;

	for(i = 0; i < num_of_connectors; i++)
	{
		if(*(unsigned int *)(mempool + offset + 60) == 1)
		{
			*(unsigned int *)(set + set_offset)		= *(unsigned int *)(mempool + offset + 48);
			*(unsigned int *)(set + set_offset +  4)	= *(unsigned int *)(mempool + offset + 52);
			*(unsigned int *)(set + set_offset +  8)	= *(unsigned int *)(mempool + offset + 44);
			*(unsigned int *)(set + set_offset + 20)	= *(unsigned int *)(mempool + offset + 32);

			mode_offsets[*num_of_active_connectors] = offset + GFX_SIZEOF_CONNECTOR + *(unsigned int *)(mempool + offset + 40) * GFX_SIZEOF_ENCODER;
			mode_sizes[*num_of_active_connectors]	= GFX_SIZEOF_MODE * *(unsigned int *)(mempool + offset + 32);
			
			set_offset += sizeof(gfx_set);

			*num_of_active_connectors += 1;
		}		
		offset += GFX_SIZEOF_CONNECTOR + 
			  *(unsigned int *)(mempool + offset + 40) * GFX_SIZEOF_ENCODER   +
			  *(unsigned int *)(mempool + offset + 32) * GFX_SIZEOF_MODE      +
			  *(unsigned int *)(mempool + offset + 36) * GFX_SIZEOF_PROPERTIES;		
	}

	*sizeof_set = set_offset;
	
	for(i = 0; i < *(unsigned int *)(mempool + 44); i++)
	{
		set_offset = 0;
		mode_offset = 0;
		
		for(k = 0; k < *num_of_active_connectors; k++)
		{
			if(*(unsigned int *)(set + set_offset + 8) == *(unsigned short *)(mempool + offset))
			{
				*(unsigned int *)(set + set_offset + 12) = *(unsigned short *)(mempool + offset + 4);
				*(unsigned int *)(set + set_offset + 16) = *(unsigned short *)(mempool + offset + 8);
				       *(void **)(set + set_offset + 24) = set + (*num_of_active_connectors * sizeof(gfx_set)) + mode_offset;
				
				memcpy(set + (*num_of_active_connectors * sizeof(gfx_set)) + mode_offset, mempool + mode_offsets[k], mode_sizes[k]);

				*sizeof_set += mode_sizes[k]; 
			}
			mode_offset += mode_sizes[k];
			set_offset  += sizeof(gfx_set);		
		}	
		offset += GFX_SIZEOF_ENCODER;
	}
}
