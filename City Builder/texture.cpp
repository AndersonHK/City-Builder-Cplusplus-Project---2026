#include "texture.h"
#include "vendor\stb_image.h"

texture::texture(const string & path)
{
	//Figure out what that is???

	const unsigned int* LocalBuffer;
	unsigned int Rederer_ID;

	//

	stbi_set_flip_vertically_on_load(1);

	glGenTextures(1,&Rederer_ID);
	glBindTexture(GL_TEXTURE_2D, Rederer_ID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,GL_RGBA, GL_UNSIGNED_BYTE, LocalBuffer);

}
