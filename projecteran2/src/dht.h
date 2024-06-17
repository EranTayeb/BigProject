#pragma once
#include "xgpio.h"

#include <stdbool.h>

typedef struct Dht_ Dht;

Dht * Dht_init(XGpio * gpio, u32 dhtPin);

bool Dht_takeData(Dht * dht);

void Dht_getResult(Dht * dht, int * temperature, int * humidity);
