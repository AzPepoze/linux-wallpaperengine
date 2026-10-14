#include "shared/core/downscale.h"

#include "test_util.h"

int main() {
    using downscale::Method;

    Method method = Method::Lanczos;
    CHECK(downscale::parse("off", method) && method == Method::Off);
    CHECK(downscale::parse("auto", method) && method == Method::Auto);
    CHECK(downscale::parse("mipmap", method) && method == Method::Mipmap);
    CHECK(downscale::parse("aniso", method) && method == Method::Aniso);
    CHECK(downscale::parse("box", method) && method == Method::Box);
    CHECK(downscale::parse("lanczos", method) && method == Method::Lanczos);
    CHECK(!downscale::parse("", method));
    CHECK(!downscale::parse("bicubic", method));
    CHECK(!downscale::parse("Box", method));
    CHECK(method == Method::Lanczos);

    CHECK(!downscale::usesMipChain(Method::Off));
    CHECK(downscale::usesMipChain(Method::Auto));
    CHECK(downscale::usesMipChain(Method::Mipmap));
    CHECK(downscale::usesMipChain(Method::Aniso));
    CHECK(!downscale::usesMipChain(Method::Box));

    CHECK(!downscale::resizesPixels(Method::Off));
    CHECK(!downscale::resizesPixels(Method::Mipmap));
    CHECK(!downscale::resizesPixels(Method::Aniso));
    CHECK(downscale::resizesPixels(Method::Box));
    CHECK(downscale::resizesPixels(Method::Lanczos));

    // Auto gives mips only to textures larger than the output in either axis.
    CHECK(downscale::wantsMips(Method::Auto, 8192, 4608, 1920, 1080));
    CHECK(downscale::wantsMips(Method::Auto, 1921, 100, 1920, 1080));
    CHECK(downscale::wantsMips(Method::Auto, 100, 1081, 1920, 1080));
    CHECK(!downscale::wantsMips(Method::Auto, 1920, 1080, 1920, 1080));
    CHECK(!downscale::wantsMips(Method::Auto, 640, 480, 1920, 1080));
    CHECK(!downscale::wantsMips(Method::Auto, 8192, 4608, 0, 0));
    CHECK(downscale::wantsMips(Method::Mipmap, 64, 64, 0, 0));
    CHECK(downscale::wantsMips(Method::Aniso, 64, 64, 1920, 1080));
    CHECK(!downscale::wantsMips(Method::Off, 8192, 4608, 1920, 1080));
    CHECK(!downscale::wantsMips(Method::Box, 8192, 4608, 1920, 1080));

    CHECK(downscale::describe(Method::Aniso) == "aniso (trilinear, 8x)");
    CHECK(downscale::describe(Method::Off) == "off (bilinear)");
    return test::finish("downscale checks");
}
