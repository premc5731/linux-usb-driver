#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#define DEVICE_NODE "/dev/usb_driver0"
#define SECTOR_SIZE 512

int main() 
{
    int fd = 0, ret = 0;
    char data[SECTOR_SIZE] = {'\0'};

    fd = open(DEVICE_NODE, O_RDWR);
    if (fd < 0) 
    {
        printf("ERROR: unable to open device file\n");
        return -1;
    }

    printf("Device opened successfully\n");
    
    printf("Reading sector 0\n");
    memset(data, '\0', SECTOR_SIZE);
    lseek(fd, 0, SEEK_SET);
    ret = read(fd, data, SECTOR_SIZE);
    if(ret <= 0)
    {
        printf("ERROR: Unable to read sector 0\n");
        return -1;
    }

    uint32_t sect_0 = ((uint8_t)data[0] << 24) | ((uint8_t)data[1] << 16) | ((uint8_t)data[2] << 8)  | ((uint8_t)data[3]);                     
    printf("sector 0 : %u\n", sect_0); 
    return 0;
}