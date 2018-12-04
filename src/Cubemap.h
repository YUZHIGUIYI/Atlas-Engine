#ifndef CUBEMAP_H
#define CUBEMAP_H

#include "System.h"

class Cubemap {

public:
	/**
	 *
	 * @param right
	 * @param left
	 * @param top
	 * @param bottom
	 * @param front
	 * @param back
	 */
	Cubemap(string right, string left, string top,
		string bottom, string front, string back);

	/**
	 *
	 * @param dataFormant
	 * @param width
	 * @param height
	 * @param internalFormat
	 * @param wrapping
	 * @param filtering
	 * @param mipmaps
	 */
	Cubemap(GLenum dataFormant, int32_t width, int32_t height, int32_t internalFormat,
			int32_t wrapping, int32_t filtering, bool mipmaps);

	/**
	 *
	 * @param unit
	 */
	void Bind(uint32_t unit);

private:
	uint32_t ID;

};

#endif