#include "chronicle.h"

#include <string.h>

void chron_add(Chronicle *c, ChronRec r)
{
    if (!c) return;
    // По полям в обнулённую запись: байты выравнивания идут в сохранение и в сумму мира
    ChronRec *d = &c->rec[c->total % CHRON_CAP];
    memset(d, 0, sizeof *d);
    d->tick = c->tick;
    d->kind = r.kind;
    d->from = r.from;
    d->to = r.to;
    d->a = r.a;
    d->b = r.b;
    d->n = r.n;
    d->m = r.m;
    d->x = r.x;
    d->z = r.z;
    c->total++;
    if (r.kind < CH_KIND_COUNT) c->counts[r.kind]++;
}

const ChronRec *chron_back(const Chronicle *c, u32 k)
{
    if (k >= c->total || k >= CHRON_CAP) return NULL;
    return &c->rec[(c->total - 1 - k) % CHRON_CAP];
}

void chron_ser(Ser *s, Chronicle *c)
{
    SER(s, c->tick);
    SER(s, c->total);
    SER(s, c->counts);
    // Записи лежат по номеру с начала: пока кольцо не заполнено — с нуля до total
    SER_N(s, c->rec, c->total < CHRON_CAP ? c->total : CHRON_CAP);
}
