#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <time.h>
#include "raw888.h"

// 이미지 데이터 (1920x720)

int setup_drm(int *fd, uint32_t *fb, uint32_t *crtc, uint32_t *conn, void **map) {
    drmModeRes *resources;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    drmModeModeInfo *mode;
    drmModeCrtc *crtc_info;

    *fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (*fd < 0) {
        perror("Cannot open /dev/dri/card0");
        return -1;
    }

    resources = drmModeGetResources(*fd);
    if (!resources) {
        perror("drmModeGetResources failed");
        close(*fd);
        return -1;
    }

    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(*fd, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED) {
            break;
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }

    if (!connector) {
        perror("No connected connector found");
        drmModeFreeResources(resources);
        close(*fd);
        return -1;
    }

    *conn = connector->connector_id;

    for (int i = 0; i < resources->count_encoders; i++) {
        encoder = drmModeGetEncoder(*fd, resources->encoders[i]);
        if (encoder->encoder_id == connector->encoder_id) {
            break;
        }
        drmModeFreeEncoder(encoder);
        encoder = NULL;
    }

    if (!encoder) {
        perror("No encoder found");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(*fd);
        return -1;
    }

    *crtc = encoder->crtc_id;
    mode = &connector->modes[0];

    uint32_t bo_handle;
    struct drm_mode_create_dumb create_dumb = {0};
    struct drm_mode_map_dumb map_dumb = {0};
    struct drm_mode_fb_cmd fb_cmd = {0};

    create_dumb.width = mode->hdisplay;
    create_dumb.height = mode->vdisplay;
    create_dumb.bpp = 32;

    if (ioctl(*fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB failed");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(*fd);
        return -1;
    }

    bo_handle = create_dumb.handle;
    fb_cmd.width = create_dumb.width;
    fb_cmd.height = create_dumb.height;
    fb_cmd.pitch = create_dumb.pitch;
    fb_cmd.bpp = create_dumb.bpp;
    fb_cmd.depth = 24;
    fb_cmd.handle = bo_handle;

    if (ioctl(*fd, DRM_IOCTL_MODE_ADDFB, &fb_cmd) < 0) {
        perror("DRM_IOCTL_MODE_ADDFB failed");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(*fd);
        return -1;
    }

    *fb = fb_cmd.fb_id;
    map_dumb.handle = bo_handle;
    if (ioctl(*fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) < 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB failed");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(*fd);
        return -1;
    }

    *map = mmap(0, create_dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, map_dumb.offset);
    if (*map == MAP_FAILED) {
        perror("mmap failed");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(*fd);
        return -1;
    }

    crtc_info = drmModeGetCrtc(*fd, encoder->crtc_id);
    if (!crtc_info) {
        perror("drmModeGetCrtc failed");
        munmap(*map, create_dumb.size);
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(*fd);
        return -1;
    }

    if (drmModeSetCrtc(*fd, encoder->crtc_id, fb_cmd.fb_id, 0, 0, &connector->connector_id, 1, mode) < 0) {
        perror("drmModeSetCrtc failed");
        munmap(*map, create_dumb.size);
        drmModeFreeCrtc(crtc_info);
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(*fd);
        return -1;
    }

    drmModeFreeCrtc(crtc_info);
    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);

    memset(*map, 0, create_dumb.size);
    return 0;
}

void draw_image(void *map, int screen_width, int screen_height, uint32_t *image) {
    for (int y = 0; y < screen_height; y++) {
        for (int x = 0; x < screen_width; x++) {
            int offset = (y * screen_width + x) * 4;
            uint32_t pixel = image[y * screen_width + x];

            ((unsigned char *)map)[offset] = (pixel & 0xFF000000)>>24;        // Blue
            ((unsigned char *)map)[offset + 1] = (pixel & 0x00FF0000) >>16; // Green
            ((unsigned char *)map)[offset + 2] = (pixel & 0x0000FF00) >> 8; // Red
            ((unsigned char *)map)[offset + 3] = (pixel & 0x000000FF); // Alpha
        }
    }
}

int main() {
    int fd;
    uint32_t fb, crtc, conn;
    void *map;
    if (setup_drm(&fd, &fb, &crtc, &conn, &map) < 0) {
        return -1;
    }

    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) {
        perror("drmModeGetResources failed");
        close(fd);
        return -1;
    }

    drmModeConnector *connector = drmModeGetConnector(fd, conn);
    if (!connector) {
        perror("drmModeGetConnector failed");
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    drmModeModeInfo *mode = &connector->modes[0];

    // 이미지 그리기
    draw_image(map, mode->hdisplay, mode->vdisplay, image1);

    // 1시간 동안 유지
    sleep(3600);

    // 정리
    munmap(map, mode->hdisplay * mode->vdisplay * 4);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    close(fd);

    return 0;
}
