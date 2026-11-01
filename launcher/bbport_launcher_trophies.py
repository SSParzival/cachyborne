# SPDX-FileCopyrightText: Copyright 2026 bbport contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher's trophies page."""

from bbport_launcher_common import *  # noqa: F401,F403


class TrophiesPage:
    """Part of LauncherWindow (bbport_launcher.py)."""

    def build_trophies_page(self):
        """The trophies the port keeps (launcher/trophy_list.py) and the player's trophy key."""
        page = Adw.PreferencesPage(description=tr(
            'The trophies earned in this saves folder. The game takes their names, descriptions and icons from its trophy package: that needs the trophy key, the same one shadPS4 asks for.'))
        key = Adw.PreferencesGroup(title=tr('Trophy key'))
        self.trophy_key_row = Adw.PasswordEntryRow(title=tr('Key (32 hexadecimal digits)'))
        self.trophy_key_row.set_text(self.settings.get("trophy_key", ""))
        self.trophy_key_row.connect("changed", self.on_trophy_key_changed)
        key.add(self.trophy_key_row)
        self.on_trophy_key_changed(self.trophy_key_row)
        page.add(key)
        self.trophy_page = page
        self.trophy_groups = []
        self.refresh_trophies()
        return page

    def on_trophy_key_changed(self, row):
        text = row.get_text().strip()
        if text and not trophy_list.valid_key(text):
            row.add_css_class("error")
        else:
            row.remove_css_class("error")
        self.settings["trophy_key"] = text

    def refresh_trophies(self):
        if not hasattr(self, "trophy_page"):
            return
        for group in self.trophy_groups:
            self.trophy_page.remove(group)
        self.trophy_groups = []
        content = trophy_list.load(self.user_dir(), self.settings.get("language", "1"))
        summary = Adw.PreferencesGroup(title=tr('Progress'))
        if content["total"] == 0:
            summary.add(Adw.ActionRow(title=tr('No trophies yet'), subtitle=tr(
                'They show here once the game has awarded the first one.')))
        else:
            got = Adw.ActionRow(title=tr('{} of {} earned').format(content["unlocked"], content["total"])
                                if content["named"] else tr('Earned: {}').format(content["unlocked"]))
            if content["named"]:
                bar = Gtk.LevelBar(min_value=0, max_value=content["total"], value=content["unlocked"],
                                   valign=Gtk.Align.CENTER, width_request=160)
                got.add_suffix(bar)
                got.set_subtitle(", ".join(
                    f"{tr(self.TROPHY_GRADES[grade])}: {content['grades'][grade][0]}/{content['grades'][grade][1]}"
                    for grade in "PGSB" if grade in content["grades"]))
            summary.add(got)
            if not content["named"]:
                summary.add(Adw.ActionRow(title=tr('Names unknown'), subtitle=tr(
                    'Enter the trophy key above and start the game: it decrypts the names and icons.')))
        self.trophy_page.add(summary)
        self.trophy_groups.append(summary)
        if content["trophies"]:
            group = Adw.PreferencesGroup(title=tr('Trophies'))
            for trophy in content["trophies"]:
                group.add(self.trophy_row(trophy, content["named"]))
            self.trophy_page.add(group)
            self.trophy_groups.append(group)

    def trophy_row(self, trophy, named):
        secret = trophy["hidden"] and not trophy["unlocked"]
        if not named:
            title = tr('Trophy #{}').format(trophy["id"])
        elif secret:
            title = tr('Hidden trophy')
        else:
            title = trophy["name"] or tr('Trophy #{}').format(trophy["id"])
        subtitle = tr('Shown once earned') if secret else trophy["detail"]
        row = Adw.ActionRow(title=GLib.markup_escape_text(title), subtitle=GLib.markup_escape_text(subtitle))
        if trophy["icon"] and not secret:
            icon = Gtk.Image.new_from_file(str(trophy["icon"]))
        else:
            icon = Gtk.Image(icon_name="starred-symbolic")
        icon.set_pixel_size(48)
        row.add_prefix(icon)
        info = []
        if trophy["grade"] in self.TROPHY_GRADES:
            info.append(tr(self.TROPHY_GRADES[trophy["grade"]]))
        if trophy["unlocked"]:
            info.append(self.trophy_time(trophy["time"]) if trophy["time"] else tr('Earned'))
        label = Gtk.Label(label="\n".join(info), justify=Gtk.Justification.RIGHT, xalign=1)
        label.add_css_class("dim-label")
        row.add_suffix(label)
        if not trophy["unlocked"]:
            row.set_opacity(0.55)
        return row

    @staticmethod
    def trophy_time(stamp):
        """The unlock time (trophies.jsonl, UTC) in local time."""
        try:
            moment = datetime.datetime.fromisoformat(stamp.replace("Z", "+00:00"))
            return moment.astimezone().strftime("%d.%m.%Y %H:%M")
        except ValueError:
            return stamp
