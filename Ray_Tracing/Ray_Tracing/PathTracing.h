#pragma once
#include <algorithm>
#include <random>

#include "Geometry.h"
#include "Constant.h"
#include "FileLoader.h"
#include "Options.h"

default_random_engine generator;
uniform_real_distribution<double> distribution(0.0, 1.0);

// N is normal: Y
// Nt : Z
// Nb : X
// Sample(world coordinate)= Sample(local) * Transform matrix
// Transform matrix:
// |Nt.x Nt.y Nt.z|
// |N.x  N.y  N.z |
// |Nb.x Nb.y Nb.z|
void createCoordinateSystem(const Vec3f &N, Vec3f &Nt, Vec3f &Nb)
{
	if (fabs(N.x) > fabs(N.y))
		Nt = Vec3f(N.z, 0, -N.x) / sqrtf(N.x * N.x + N.z * N.z);		
	else
		Nt = Vec3f(0, -N.z, N.y) / sqrtf(N.y * N.y + N.z * N.z);
	Nb = N.crossProduct(Nt);
}

// r1 and r2 are subject to [0, 1] Uniform distribuation
Vec3f uniformSampleHemisphere(const float &r1, const float &r2)
{
	// cos(theta) = u1 = y
	// cos^2(theta) + sin^2(theta) = 1 -> sin(theta) = srtf(1 - cos^2(theta))
	float sinTheta = sqrtf(1 - r1 * r1);
	float phi = 2 * M_PI * r2;
	float x = sinTheta * cosf(phi);
	float z = sinTheta * sinf(phi);
	return Vec3f(x, r1, z);
}

Vec3f reflect(const Vec3f &I, const Vec3f &N)
{
	return I - 2 * I.dotProduct(N) * N;
}

Vec3f refract(const Vec3f &I, const Vec3f &N, const float &ior)
{
	float cosi=min(max(float(-1.0), I.dotProduct(N)), float(1.0));
	float etai = 1, etat = ior;
	Vec3f n = N;
	if (cosi < 0) { cosi = -cosi; }
	else { std::swap(etai, etat); n = -N; }
	float eta = etai / etat;
	float k = 1 - eta * eta * (1 - cosi * cosi);
	return k < 0 ? 0 : eta * I + (eta * cosi - sqrtf(k)) * n;
}

Vec3f castRay(
	const Vec3f &orig, const Vec3f &dir,
	Scene &scene,
	const Options &options,
	const int depth = 0)
{
	if (depth>options.maxDepth)
		return options.backgroundColor;

	Vec3f hitColor = options.backgroundColor;
	float tnear = kInfinity;
	Vec2f uv;
	int index = 0;
	Object hitObject;
	if (scene.intersect(orig, dir, tnear, index, uv, hitObject))
	{
		// get the surface properties, including:
		//		uv Coordinates
		//		Hit Normal
		//		Texture Coordinates
		//		Color
		//		Material
		Vec3f hitPoint = orig + dir * tnear;
		Vec3f hitNormal;
		Vec2f hitTexCoordinates;
		Vec3f Color;
		Material *m = nullptr;
		hitObject.getSurfaceProperties(hitPoint, dir, index, uv, hitNormal, hitTexCoordinates, Color, m);

		// If the hit point is light source
		if (m->self_luminous)
			return m->ka*Color;

		// If it's diffuse
		if (m->diffuse)
		{
			Vec3f indirectLigthing = 0;
			int N = 128;// / (depth + 1);
			Vec3f Nt, Nb;
			createCoordinateSystem(hitNormal, Nt, Nb);
			float pdf = 1 / (2 * M_PI);
			for (int n = 0; n < N; ++n) {
				float r1 = distribution(generator);
				float r2 = distribution(generator);
				Vec3f sample = uniformSampleHemisphere(r1, r2);
				Vec3f sampleWorld(
					sample.x * Nb.x + sample.y * hitNormal.x + sample.z * Nt.x,
					sample.x * Nb.y + sample.y * hitNormal.y + sample.z * Nt.y,
					sample.x * Nb.z + sample.y * hitNormal.z + sample.z * Nt.z);
				// don't forget to divide by PDF and multiply by cos(theta)
				indirectLigthing += r1 * castRay(hitPoint, sampleWorld, scene, options, depth + 1) / pdf;
			}
			// divide by N
			indirectLigthing /= (float)N;
			hitColor += indirectLigthing*m->kd;
		}

		// If it's specular
		if (m->specular)
		{
			Vec3f Reflect = reflect(dir, hitNormal).normalize();
			Vec3f lightIntensity = castRay(hitPoint, Reflect, scene, options, depth + 1);
			float cosineAlpha = Reflect.dotProduct(-dir);
			hitColor += lightIntensity * m->ks * pow(cosineAlpha, m->ns.exponent);
		}

		// If it's transparent
		if (m->transparent)
		{
			Vec3f refractionColor = 0, reflectionColor = 0;
			// compute fresnel
			float kr = m->tr.ratio;
			bool outside = dir.dotProduct(hitNormal) < 0;
			Vec3f bias = options.bias * hitNormal;
			// compute refraction 	
			Vec3f refractionDirection = refract(dir, hitNormal, m->ni.optical_density).normalize();
			refractionColor = castRay(hitPoint, refractionDirection, scene, options, depth + 1);
			// compute reflect 	
			Vec3f reflectionDirection = reflect(dir, hitNormal).normalize();
			reflectionColor = castRay(hitPoint, reflectionDirection, scene, options, depth + 1);

			// mix the two
			hitColor += reflectionColor * kr + refractionColor * (1 - kr);
		}
	}
	return hitColor;
}

void render(
	const Options &options,
	Scene &scene,
	Vec3f *pixels)
{
	float scale = tan(options.fov * 0.5*M_PI / 180);
	float imageAspectRatio = options.width / (float)options.height;
	Vec3f orig;
	options.cameraToWorld.multVecMatrix(Vec3f(0), orig);
	for (int j = 0; j < options.height; ++j) {
		for (int i = 0; i < options.width; ++i) {
			// generate primary ray direction
			float x = (2 * (i + 0.5) / (float)options.width - 1) * imageAspectRatio * scale;
			float y = (1 - 2 * (j + 0.5) / (float)options.height) * scale;
			Vec3f dir;
			options.cameraToWorld.multDirMatrix(Vec3f(x, y, -1), dir);
			dir.normalize();
			
			*pixels++ = castRay(orig, dir, scene, options, 0);// pixel++!!!!!!!!!!!!!!!!! 
		}
	}
}