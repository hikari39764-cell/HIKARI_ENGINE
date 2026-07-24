#include <spine/Extension.h>

namespace spine {
    SP_API SpineExtension* getDefaultExtension() {
        return new DefaultSpineExtension();
    }
}
