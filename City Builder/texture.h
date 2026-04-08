#pragma once

#include <string>
#include <GL/glew.h>

using namespace std;

class texture
{
private:
	string filepath;
	int width, height, bpp;
public:
	texture(const string & path);
};