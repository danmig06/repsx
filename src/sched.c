#include <psx/sched.h>

void psx_sched_init(struct psx_sched* sched) {
	sched->clocks_elapsed = 0;
	sched->ev_list = NULL;
}

void psx_sched_add_ev(struct psx_sched* sched, struct psx_sev* ev) {
	ev->clocks_left = sched->clocks_elapsed + ev->eta;
	if(!sched->ev_list) {
		sched->ev_list = ev;
		return;
	}

	struct psx_sev* prev = sched->ev_list;
	if(prev->next == NULL) {
		if(ev->clocks_left < prev->clocks_left) {
			ev->next = prev;
			sched->ev_list = ev;
		} else {
			prev->next = ev;
			ev->next = NULL;	
		}
		return;
	}

	struct psx_sev* current = sched->ev_list->next;
	while(1) {
		if(ev->clocks_left < current->clocks_left) {
			prev->next = ev;
			ev->next = current;
			break;
		}
		if(current->next == NULL) {
			current->next = ev;
			ev->next = NULL;
			break;
		}
		prev = current;
		current = current->next;
	}
}

void psx_sched_remove_ev(struct psx_sched* sched, uint8_t id) {
	if(sched->ev_list->id == id) {
		sched->ev_list = sched->ev_list->next;
		return;
	}

	struct psx_sev* current;
	struct psx_sev* target;
	for(current = sched->ev_list; current->next != NULL; current = current->next) {
		target = current->next;
		if(target->id == id) {
			current->next = current->next->next;
			target->next = NULL;
			break;
		}
	}
}

void psx_sched_update(struct psx_sched* sched, uint32_t clocks) {
	if(!sched->ev_list) {
		sched->clocks_elapsed = 0;
		return;
	}

	sched->clocks_elapsed += clocks;
	struct psx_sev* ev = sched->ev_list;
	while(ev && sched->clocks_elapsed >= ev->clocks_left) {
		ev->trigger(sched, ev);
		ev = ev->next;
	}
}
