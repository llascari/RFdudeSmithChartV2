#pragma once

#include "RfModel.h"

#include <QByteArray>
#include <QString>

namespace ltspice {

QByteArray schematic(const rf::Document& document);
void save(const QString& path, const rf::Document& document);

}
