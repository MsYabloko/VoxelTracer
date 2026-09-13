#ifndef VOXELTRACER_TEXTURE_H
#define VOXELTRACER_TEXTURE_H

#include <iostream>
#include <vector>

class Texture
{
    unsigned int id;
    bool isLoaded = false;
    bool isCubemap = false;
public:
    void open(const char* path );
    void open_cubemap(std::vector<std::string> faces); //Right, Left, Top, Bottom, Front, Back
    void use(unsigned int num);
};

#endif
