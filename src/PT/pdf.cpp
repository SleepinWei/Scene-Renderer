#include <PT/onb.h>
#include <PT/pdf.h>

double sphere_pdf::value(const vec3 &direction) const
{
    return 1 / (4 * PI);
}

vec3 sphere_pdf::generate() const
{
    return random_unit_vector();
}

cosine_pdf::cosine_pdf(const vec3 &w) { uvw.build_from_w(w); }

double cosine_pdf::value(const vec3 &direction) const
{
    auto cosine_theta = glm::dot(glm::normalize(direction), uvw.w());
    return fmax(0, cosine_theta / PI);
}

vec3 cosine_pdf::generate() const
{
    return uvw.local(random_cosine_direction());
}