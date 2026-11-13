/*
===========================================================================
ef-launcher (Qt 6): levelshot decoding (JPEG through the engine's bundled
libjpeg, TGA by hand: Qt is built without image-format plugins other than PNG).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef EF_QT_LEVELSHOT_H
#define EF_QT_LEVELSHOT_H

#include <QImage>
#include <QPixmap>

extern "C" {
#include "lc_server.h"
}

namespace Ui {

/* decode raw JPEG / TGA / PNG bytes; a null image on failure */
QImage decodeImage( const unsigned char *data, size_t len, bool isTga );
/* the thumbnail of a catalog map (cached), 56x42 points; null if it has no levelshot */
QPixmap levelshotThumb( const lc_map_info *m, qreal dpr );

}

#endif
