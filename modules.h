#include "element.h"

struct cpu_data {
	unsigned int user, nice, system, idle, total;
};

void volume(struct element *ctx);
void sleep_state(struct element *ctx);
void kblayout(struct element *ctx);
void memory(struct element *ctx);
void cpu(struct element *ctx);
void temperature(struct element *ctx);
void power(struct element *ctx);
void battery(struct element *ctx);
void wifi(struct element *ctx);
void date(struct element *ctx);
