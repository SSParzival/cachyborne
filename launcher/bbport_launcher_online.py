# SPDX-FileCopyrightText: Copyright 2026 bbport contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher's online page: server pool, THD account, connection test."""

from bbport_launcher_common import *  # noqa: F401,F403


class OnlinePage:
    """Part of LauncherWindow (bbport_launcher.py)."""

    def build_online_page(self):
        s = self.settings
        page = Adw.PreferencesPage()
        play = Adw.PreferencesGroup(description=tr(
            "Messages, bloodstains and ghosts come from the game server; bells and summons go through a shadNet server. By default both are Seamless Hunt's (BBSC's server). Online play works together with the Windows version: everyone needs game version 1.09 and the same server"))
        self.online_row = Adw.SwitchRow(title=tr('Play online'),
                                        subtitle=tr('Off: the game stays offline, as before'),
                                        active=s["online"])
        play.add(self.online_row)
        if not online_module():
            self.online_row.set_sensitive(False)
            self.online_row.set_subtitle(tr('This build has no online module (libbbnet20.so): offline play only'))
        self.pool_row = combo_row(tr('Pool'), tr('Who to play with: everyone in a session must use the same pool'), [
            ("Seamless Hunt (seamlesshunt.com)", "seamless",
             'BBSC server: seamless co-op, bells and summons; the account is made on the website'),
            ("The Hunter's Dream (thehuntersdream.com)", "huntersdream",
             "Co-op with bbhost players through The Hunter's Dream; sign in with Discord"),
        ], "huntersdream" if s.get("online_pool") == "huntersdream" else "seamless")
        play.add(self.pool_row)
        page.add(play)

        game = Adw.PreferencesGroup(title=tr('Game server (messages, bloodstains, ghosts)'))
        self.game_group = game
        self.community_row = Adw.EntryRow(title=tr('Game server'), text=s["online_community"])
        game.add(self.community_row)
        page.add(game)

        coop = None
        coop = Adw.PreferencesGroup(
            title=tr('Co-op server (bells and summons)'),
            description=tr("play.seamlesshunt.com:31313 is Seamless Hunt's server (BBSC). shadPS4's public server accepts only shadPS4 builds. For a private server, enter host:port from the person who runs it. WebAPI: leave it empty to use the server address with port 31315"))
        self.server_row = Adw.EntryRow(title=tr('Server address'), text=s["online_server"])
        self.webapi_row = Adw.EntryRow(title=tr('WebAPI address'), text=s["online_webapi"])
        coop.add(self.server_row)
        coop.add(self.webapi_row)
        self.bbsc_row = Adw.SwitchRow(
            title=tr('Seamless co-op (BBSC)'),
            subtitle=tr('The session survives deaths, lamps, area changes and bosses; up to 5 hunters. Needs a server running the BBSC game server, and every player turns it on') + ". " +
                     tr("While it is on, BBSC's bells replace the game's; turn it off and the original bells come back the next time you load your character (the save is backed up in save-backups)"),
            active=s.get("online_bbsc", False))
        coop.add(self.bbsc_row)
        self.coop_group = coop
        page.add(coop)

        account = Adw.PreferencesGroup(
            title=tr('Account'),
            description=tr('Your shadNet account name, not your email. Register at seamlesshunt.com or ask the owner of a private server. The password is saved on this computer with the launcher settings'))
        self.npid_row = Adw.EntryRow(title=tr("Online ID (NPID)"), text=s["online_npid"])
        self.password_row = Adw.PasswordEntryRow(title=tr('Password'), text=s["online_password"])
        account.add(self.npid_row)
        account.add(self.password_row)
        self.account_group = account
        page.add(account)

        thd = Adw.PreferencesGroup(
            title=tr("The Hunter's Dream"),
            description=tr("Discord sign-in as in bbhost: confirm a code on the website. No password; the token is kept on this computer with the launcher's settings"))
        self.thd_server_row = Adw.EntryRow(title=tr('Server address'), text=s.get("thd_server", ""))
        self.thd_server_row.set_tooltip_text(online.HUNTERS_DREAM_SERVER)
        thd.add(self.thd_server_row)
        self.thd_account_row = Adw.ActionRow()
        self.thd_sign_button = Gtk.Button(valign=Gtk.Align.CENTER)
        self.thd_sign_button.connect("clicked", self.on_thd_sign)
        self.thd_account_row.add_suffix(self.thd_sign_button)
        thd.add(self.thd_account_row)
        self.thd_group = thd
        self.thd_id, self.thd_token = s.get("thd_online_id", ""), s.get("thd_token", "")
        self.thd_signing = False
        self.thd_cancel = False
        self.show_thd_account()
        page.add(thd)

        connection = Adw.PreferencesGroup(
            title=tr('Connection'),
            description=tr('Everyone in a session needs the same game version (1.09) and the same server. Cheats change the game for the other players too: turn them off when you play together'))
        self.upnp_row = Adw.SwitchRow(title=tr('UPnP (open a port on the router automatically)'),
                                      subtitle=tr('Turn it off with Tailscale or another VPN'),
                                      active=s["online_upnp"])
        connection.add(self.upnp_row)
        test = Adw.ActionRow(title=tr('Test the connection'))
        self.test_button = Gtk.Button(label=tr('Test'), valign=Gtk.Align.CENTER)
        self.test_button.connect("clicked", self.on_test_connection)
        test.add_suffix(self.test_button)
        connection.add(test)
        self.test_rows = []
        self.connection_group = connection
        page.add(connection)
        self.pool_row.connect("notify::selected", self.on_pool_changed)
        self.on_pool_changed()
        return page

    def online_pool(self):
        return combo_value(self.pool_row)

    def on_pool_changed(self, *_):
        pool = self.online_pool()
        legacy, thd = pool == "seamless", pool == "huntersdream"
        self.game_group.set_visible(not thd)
        self.coop_group.set_visible(not thd)
        self.account_group.set_visible(not thd)
        self.bbsc_row.set_visible(legacy)
        self.thd_group.set_visible(thd)
        if not online_module(pool):
            self.pool_row.set_subtitle(tr('This build has no module for this pool (libbbnet20.so)'))

    def show_thd_account(self):
        link = getattr(self, "thd_link", None)
        if link is not None:
            self.thd_account_row.remove(link)
            self.thd_link = None
        if self.thd_id and self.thd_token:
            self.thd_account_row.set_title(tr('Signed in as {}').format(GLib.markup_escape_text(self.thd_id)))
            self.thd_account_row.set_subtitle("")
            self.thd_sign_button.set_label(tr('Sign out'))
        else:
            self.thd_account_row.set_title(tr('Not signed in'))
            self.thd_account_row.set_subtitle(tr("A Discord account linked to The Hunter's Dream is needed"))
            self.thd_sign_button.set_label(tr('Sign in with Discord'))
        if self.thd_signing:
            self.thd_sign_button.set_label(tr('Cancel'))
        self.thd_sign_button.set_sensitive(True)

    def on_thd_sign(self, _button):
        if self.thd_signing:
            self.thd_cancel = True  # the waiting thread stops; done() resets the row
            return
        self.thd_cancel = False
        if self.thd_id and self.thd_token:
            self.thd_id = self.thd_token = ""
            self.show_thd_account()
            return
        self.thd_signing = True
        self.show_thd_account()
        server = self.thd_server_row.get_text().strip()

        def show_code(page, code):
            def show():
                self.thd_account_row.set_title(tr('Sign-in code: {}').format(code))
                self.thd_account_row.set_subtitle(GLib.markup_escape_text(tr(
                    'On {}: 1) press “Sign in with Discord to link” and sign in; 2) back on the page, enter the code in the code field (not in “Sign in with a code from the game”). The code is on the clipboard').format(page)))
                # The code goes to the clipboard; the page opens by whatever works in this environment
                # (an AppImage may have no default handler for the GIO call), else a button does it.
                try:
                    self.thd_account_row.get_clipboard().set(code)
                except Exception:
                    pass
                if not open_url(page):
                    link = Gtk.LinkButton(uri=page, label=tr('Open the page'), valign=Gtk.Align.CENTER)
                    self.thd_account_row.add_prefix(link)
                    self.thd_link = link
                return False
            GLib.idle_add(show)

        def done(name, token, error):
            self.thd_signing = False
            if error:
                self.show_thd_account()
                if error != "cancelled":
                    self.thd_account_row.set_subtitle(GLib.markup_escape_text(error))
            else:
                self.thd_id, self.thd_token = name, token
                self.settings["thd_online_id"], self.settings["thd_token"] = name, token
                save_settings(self.settings)
                self.show_thd_account()
            return False

        def work():
            try:
                name, token = online.thd_sign_in(server, show_code, device="bbport on Linux",
                                                  cancelled=lambda: self.thd_cancel)
            except ValueError as error:
                GLib.idle_add(done, "", "", str(error))
                return
            GLib.idle_add(done, name, token, "")
        threading.Thread(target=work, daemon=True).start()

    def online_addresses(self):
        server = self.server_row.get_text().strip() or online.SHADNET_SERVER
        community = online.community_address(self.community_row.get_text())
        webapi = online.webapi_setting(self.webapi_row.get_text(), server, community)
        return server, webapi, community

    def on_test_connection(self, _button):
        if self.online_pool() == "huntersdream":
            self.test_thd_connection()
            return
        server, webapi, community = self.online_addresses()
        names = {"game": tr('Game server'), "shadnet": tr('shadNet server'), "webapi": "WebAPI"}
        for row in self.test_rows:
            self.connection_group.remove(row)
        self.test_rows = []
        self.test_button.set_sensitive(False)
        self.test_button.set_label(tr('Checking…'))

        def show(results):
            for name, address, ok, detail in results:
                # Plain text: an error such as "<urlopen error ...>" is not Pango markup.
                row = Adw.ActionRow(title=GLib.markup_escape_text(f"{names[name]}: {address}"),
                                    subtitle=GLib.markup_escape_text(
                                        tr('reachable') if ok else f'{tr('not reachable')} ({detail})'))
                row.add_prefix(Gtk.Image(icon_name="object-select-symbolic" if ok else "dialog-warning-symbolic"))
                self.connection_group.add(row)
                self.test_rows.append(row)
            self.test_button.set_sensitive(True)
            self.test_button.set_label(tr('Test'))
            return False

        def work():
            results = online.check(server, webapi, community)
            GLib.idle_add(show, results)
        threading.Thread(target=work, daemon=True).start()

    def test_thd_connection(self):
        names = {"game": tr('Game server'), "service": tr('Co-op service')}
        server = self.thd_server_row.get_text().strip()
        for row in self.test_rows:
            self.connection_group.remove(row)
        self.test_rows = []
        self.test_button.set_sensitive(False)
        self.test_button.set_label(tr('Checking…'))

        def show(results):
            for name, address, ok, detail in results:
                row = Adw.ActionRow(title=GLib.markup_escape_text(f"{names[name]}: {address}"),
                                    subtitle=GLib.markup_escape_text(
                                        tr('reachable') if ok else f'{tr('not reachable')} ({detail})'))
                row.add_prefix(Gtk.Image(icon_name="object-select-symbolic" if ok else "dialog-warning-symbolic"))
                self.connection_group.add(row)
                self.test_rows.append(row)
            self.test_button.set_sensitive(True)
            self.test_button.set_label(tr('Test'))
            return False

        threading.Thread(target=lambda: GLib.idle_add(show, online.check_thd(server)), daemon=True).start()

    # --- trophies page ------------------------------------------------------------------

    TROPHY_GRADES = {"P": 'Platinum', "G": 'Gold', "S": 'Silver', "B": 'Bronze'}
