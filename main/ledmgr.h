#pragma once
#include "app.h"

void ledmgr_init(void);
void ledmgr_on_disconnected(void);
void ledmgr_on_connecting(void);
void ledmgr_on_connected(void);
void ledmgr_on_activity(void);
void ledmgr_on_panic(panic_id_t id);
