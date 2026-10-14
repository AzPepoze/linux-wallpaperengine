#include "shared/core/downscale.h"

#include "test_util.h"

int main() {
    using downscale::Method;

    Method method = Method::Lanczos;
    CHECK(downscale::parse("off", method) && method == Method::Off);
    CHECK(downscale::parse("mipmap", method) && method == Method::Mipmap);
    CHECK(downscale::parse("aniso", method) && method == Method::Aniso);
    CHECK(downscale::parse("box", method) && method == Method::Box);
    CHECK(downscale::parse("lanczos", method) && method == Method::Lanczos);
    CHECK(!downscale::parse("", method));
    CHECK(!downscale::parse("bicubic", method));
    CHECK(!downscale::parse("Box", method));
    CHECK(method == Method::Lanczos);

    CHECK(!downscale::usesMipChain(Method::Off));
    CHECK(downscale::usesMipChain(Method::Mipmap));
    CHECK(downscale::usesMipChain(Method::Aniso));
    CHECK(!downscale::usesMipChain(Method::Box));

    CHECK(!downscale::resizesPixels(Method::Off));
    CHECK(!downscale::resizesPixels(Method::Mipmap));
    CHECK(!downscale::resizesPixels(Method::Aniso));
    CHECK(downscale::resizesPixels(Method::Box));
    CHECK(downscale::resizesPixels(Method::Lanczos));

    CHECK(downscale::describe(Method::Aniso) == "aniso (trilinear, 8x)");
    CHECK(downscale::describe(Method::Off) == "off (bilinear)");
    return test::finish("downscale checks");
}
