// Bandicoot: portable immediate-mode replacements for the handful of
// freeglut geometry primitives Coot used (glutWireSphere / glutSolidTorus).
//
// Bandicoot no longer links freeglut on macOS: it dragged in Mesa libGL +
// an X11 stack and forced an XQuartz launch on startup, and freeglut 3.x
// aborts these very calls unless glutInit() ran first. These helpers emit
// the same GL geometry with the same GLUT argument semantics.

#ifndef BANDICOOT_GL_PRIMITIVES_HH
#define BANDICOOT_GL_PRIMITIVES_HH

#include <cmath>

#ifdef __APPLE__
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

namespace coot {

   // Equivalent of glutWireSphere(radius, slices, stacks): a wire sphere
   // centred at the origin, drawn as meridian line-strips + parallel
   // line-loops, with outward normals.
   inline void gl_wire_sphere(double radius, int slices, int stacks) {
      if (slices < 3) slices = 3;
      if (stacks < 2) stacks = 2;
      for (int i = 0; i < slices; i++) {          // meridians (longitude)
         double lon = 2.0 * M_PI * i / slices;
         double cl = std::cos(lon), sl = std::sin(lon);
         glBegin(GL_LINE_STRIP);
         for (int j = 0; j <= stacks; j++) {
            double lat = M_PI * (-0.5 + static_cast<double>(j) / stacks);
            double xy = std::cos(lat), z = std::sin(lat);
            glNormal3d(xy * cl, xy * sl, z);
            glVertex3d(radius * xy * cl, radius * xy * sl, radius * z);
         }
         glEnd();
      }
      for (int j = 1; j < stacks; j++) {          // parallels (latitude)
         double lat = M_PI * (-0.5 + static_cast<double>(j) / stacks);
         double xy = std::cos(lat), z = std::sin(lat);
         glBegin(GL_LINE_LOOP);
         for (int i = 0; i < slices; i++) {
            double lon = 2.0 * M_PI * i / slices;
            double cl = std::cos(lon), sl = std::sin(lon);
            glNormal3d(xy * cl, xy * sl, z);
            glVertex3d(radius * xy * cl, radius * xy * sl, radius * z);
         }
         glEnd();
      }
   }

   // Equivalent of glutSolidTorus(innerRadius, outerRadius, nsides, nrings):
   // inner = tube (cross-section) radius, outer = distance from the torus
   // centre to the tube centre. Drawn as lit quad-strips (matches the classic
   // GLUT/Mesa tessellation and normals).
   inline void gl_solid_torus(double inner, double outer, int nsides, int nrings) {
      if (nsides < 3) nsides = 3;
      if (nrings < 3) nrings = 3;
      double ringDelta = 2.0 * M_PI / nrings;
      double sideDelta = 2.0 * M_PI / nsides;
      double theta = 0.0, cosTheta = 1.0, sinTheta = 0.0;
      for (int i = nrings - 1; i >= 0; i--) {
         double theta1 = theta + ringDelta;
         double cosTheta1 = std::cos(theta1), sinTheta1 = std::sin(theta1);
         glBegin(GL_QUAD_STRIP);
         double phi = 0.0;
         for (int j = nsides; j >= 0; j--) {
            phi += sideDelta;
            double cosPhi = std::cos(phi), sinPhi = std::sin(phi);
            double dist = outer + inner * cosPhi;
            glNormal3d(cosTheta1 * cosPhi, -sinTheta1 * cosPhi, sinPhi);
            glVertex3d(cosTheta1 * dist, -sinTheta1 * dist, inner * sinPhi);
            glNormal3d(cosTheta * cosPhi, -sinTheta * cosPhi, sinPhi);
            glVertex3d(cosTheta * dist, -sinTheta * dist, inner * sinPhi);
         }
         glEnd();
         theta = theta1; cosTheta = cosTheta1; sinTheta = sinTheta1;
      }
   }

}

#endif // BANDICOOT_GL_PRIMITIVES_HH
