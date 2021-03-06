#include "widgets.h"
#include "volume.h"

static int
widget_update (struct widget *widget, snd_mixer_elem_t *elem) {
	long volume_min, volume_max, volume;
	int active;

	snd_mixer_selem_get_playback_volume_range(elem, &volume_min, &volume_max);
	snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &volume);
	snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_FRONT_LEFT, &active);

	widget_data_callback(widget,
	                     widget_data_arg_number(100 * (volume - volume_min) / (volume_max - volume_min)),
	                     widget_data_arg_boolean(active));

	return 0;
}

void*
widget_main (struct widget *widget) {
	struct widget_config config = widget_config_defaults;
	widget_init_config_string(widget->config, "card", config.card);
	widget_init_config_string(widget->config, "selem", config.selem);
	widget_epoll_init(widget);

	/* open mixer */
	int err = 0;
	int mixer_fd;
	snd_mixer_selem_id_t *sid = NULL;
	snd_mixer_t *mixer = NULL;
	struct epoll_event mixer_event;
	struct pollfd *pollfds = NULL;
	unsigned short i;

	if ((err = snd_mixer_open(&mixer, 0)) < 0) {
		LOG_ERR("could not open mixer (%i)", err);
		goto cleanup;
	}
	if ((err = snd_mixer_attach(mixer, config.card)) < 0) {
		LOG_ERR("could not attach card '%s' to mixer (%i)", config.card, err);
		goto cleanup;
	}
	if ((err = snd_mixer_selem_register(mixer, NULL, NULL)) < 0) {
		LOG_ERR("could not register mixer simple element class (%i)", err);
		goto cleanup;
	}
	if ((err = snd_mixer_load(mixer)) < 0) {
		LOG_ERR("could not load mixer (%i)", err);
		goto cleanup;
	}

	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, config.selem);
	snd_mixer_elem_t *elem = snd_mixer_find_selem(mixer, sid);

	if (!elem) {
		LOG_ERR("could not find selem '%s'", config.selem);
		goto cleanup;
	}

	int pollfds_len = snd_mixer_poll_descriptors_count(mixer);
	pollfds = calloc(pollfds_len, sizeof(*pollfds));
	err = snd_mixer_poll_descriptors(mixer, &pollfds[0], pollfds_len);
	if (err < 0) {
		LOG_ERR("alsa: can't get poll descriptors: %i", err);
	}

	for (i = 0; i < pollfds_len; i++) {
		mixer_fd = pollfds[i].fd;
		mixer_event.data.fd = mixer_fd;
		mixer_event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_ADD, mixer_fd, &mixer_event) == -1) {
			LOG_ERR("failed to add fd to epoll instance: %s", strerror(errno));

			return 0;
		}
	}

	widget_update(widget, elem);
	while (true) {
		while ((nfds = epoll_wait(efd, events, MAX_EVENTS, -1)) > 0) {
			for (i = 0; i < nfds; i++) {
				if (events[i].data.fd == widget->bar->efd) {
					goto cleanup;
				}
			}
			snd_mixer_handle_events(mixer);
			widget_update(widget, elem);
		}
	}

cleanup:
	if (pollfds != NULL) {
		free(pollfds);
	}
	if (mixer != NULL) {
		snd_mixer_close(mixer);
	}

	widget_epoll_cleanup(widget);
	widget_clean_exit(widget);
}
