#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#define DEVICE_NODE "/dev/usb_driver0"
#define SECTOR_SIZE 512

int main() {
    int fd = 0, ret = 0;
    char data[SECTOR_SIZE] = {'\0'};

    printf("Testing the usb_driver...\n");

    ///////////////////////////////////////////////////////
    fd = open(DEVICE_NODE, O_RDWR);
    if (fd < 0) 
    {
        printf("ERROR: unable to open device file\n");
        return -1;
    }

    printf("Device opened successfully\n");

    ///////////////////////////////////////////////////////
    printf("Writing to sector 1\n");
    snprintf(data, SECTOR_SIZE, "Hello to sector 1\n");
    ret = write(fd,data, SECTOR_SIZE);
    if(ret <= 0)
    {
        printf("ERROR: Unable to write in sector 1\n");
        return -1;
    }

    ///////////////////////////////////////////////////////
    printf("Writing to sector 2\n");
    memset(data, '\0', SECTOR_SIZE);
    snprintf(data, SECTOR_SIZE, "Hello to sector 2\n");
    ret = write(fd,data, SECTOR_SIZE);
    if(ret <= 0)
    {
        printf("ERROR: Unable to write in sector 2\n");
        return -1;
    }

    ///////////////////////////////////////////////////////
    printf("Writing to sector 3\n");
    memset(data, '\0', SECTOR_SIZE);
    snprintf(data, SECTOR_SIZE, "Hello to sector 3\n");
    ret = write(fd,data, SECTOR_SIZE);
    if(ret <= 0)
    {
        printf("ERROR: Unable to write in sector 3\n");
        return -1;
    }

    ///////////////////////////////////////////////////////
    printf("Reading sector 1\n");
    memset(data, '\0', SECTOR_SIZE);
    lseek(fd, 1, SEEK_SET);
    printf("Data from sector 1\n");
    ret = read(fd,data,SECTOR_SIZE);
    if(ret <= 0)
    {
        printf("ERROR: Unable to read in sector 1\n");
        return -1;
    }
    data[SECTOR_SIZE - 1] = '\0';
    printf("Data: %s",data);

    ///////////////////////////////////////////////////////
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

    ///////////////////////////////////////////////////////
    printf("updating(writing) sector 1\n");
    memset(data, '\0', SECTOR_SIZE);
    lseek(fd, 1, SEEK_SET);
    snprintf(data, SECTOR_SIZE, "Updated message Hello to sector 1\n");
    ret = write(fd,data, SECTOR_SIZE);
    if(ret <= 0)
    {
        printf("ERROR: Unable to write in sector 1\n");
        return -1;
    }

    ///////////////////////////////////////////////////////
    printf("Reading updated sector 1\n");
    memset(data, '\0', SECTOR_SIZE);
    lseek(fd, 1, SEEK_SET);
    printf("Data from sector 1 : \n");
    ret = read(fd,data,SECTOR_SIZE);
    if(ret <= 0)
    {
        printf("ERROR: Unable to read in sector 1\n");
        return -1;
    }
    printf("Data: %s",data);

    ///////////////////////////////////////////////////////
    printf("\n");

    close(fd);
    printf("\nTesting Completed...\n");
    return 0;
}