#ifndef PDF_H
#define PDF_H
#include<PT/glm_vec3.h>
#include<memory>
#include"PT/hittable.h"
#include<PT/onb.h>
using std::shared_ptr; 

class onb;


class pdf
{
public:
    virtual ~pdf() {}
    virtual double value(const vec3 &direction) const = 0;
    virtual vec3 generate() const = 0;
};

class sphere_pdf : public pdf
{
public:
    sphere_pdf() {}

    double value(const vec3 &direction) const override;

    vec3 generate() const override;
};

class cosine_pdf : public pdf
{
public:
    cosine_pdf(const vec3 &w);

    double value(const vec3 &direction) const override;

    vec3 generate() const override;

private:
    onb uvw;
};

class hittable_pdf : public pdf {
  public:
    hittable_pdf(shared_ptr<hittable>_objects, const vec3& _origin)
      : objects(_objects), origin(_origin)
    {}

    double value(const vec3& direction) const override {
        return objects->pdf_value(origin, direction);
    }

    vec3 generate() const override {
        return objects->random(origin);
    }

  private:
    shared_ptr<hittable> objects;
    vec3 origin;
};

class mixture_pdf : public pdf {
    public:
        mixture_pdf(shared_ptr<pdf> p0, shared_ptr<pdf> p1) {
            p[0] = p0;
            p[1] = p1;
        }

        mixture_pdf(){
            p[0] = p[1] = nullptr;
        }

        virtual double value(const vec3& direction) const override {
            return 0.5 * p[0]->value(direction) + 0.5 *p[1]->value(direction);
        }

        virtual vec3 generate() const override {
            if (random_double() < 0.5)
                return p[0]->generate();
            else
                return p[1]->generate();
        }

    public:
        shared_ptr<pdf> p[2];
};

#endif // !PDF_H
