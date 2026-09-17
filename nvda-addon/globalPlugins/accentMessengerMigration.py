"""One-time migration from the experimental package, following Panthera's lifecycle."""
import addonHandler
import globalPluginHandler
import globalVars
import gui
from logHandler import log
import wx

OLD_NAMES = frozenset(('messengerExperimental', 'accent-messenger-experimental'))


def conflicts():
    # Include disabled copies too: re-enabling either package would shadow the
    # shared synth module. Never remove unrelated add-ons or the new package.
    return [a for a in list(addonHandler.getAvailableAddons())
            if a.manifest.get('name') in OLD_NAMES]


class GlobalPlugin(globalPluginHandler.GlobalPlugin):
    def __init__(self):
        super().__init__()
        self._timer = None
        self._closed = False
        if not getattr(globalVars.appArgs, 'secure', False):
            self._timer = wx.CallLater(1500, self._ask)

    def _ask(self):
        if self._closed:
            return
        try:
            old = conflicts()
            if not old:
                return
            title = 'Accent Messenger migration'
            names = ', '.join(a.manifest.get('summary', a.manifest['name']) for a in old)
            answer = gui.messageBox(
                'Accent Messenger replaces the experimental add-on: %s.\n\n'
                'Both packages provide the same synthesizer and cannot be used together. '
                'Remove the experimental package? Your Messenger voice settings will remain.\n\n'
                'If you choose No, this question will return the next time NVDA starts.' % names,
                title, wx.YES_NO | wx.ICON_WARNING)
            if answer != wx.YES:
                return
            failed = []
            for addon in old:
                try:
                    addon.requestRemove()
                except Exception:
                    failed.append(addon.manifest['name'])
                    log.exception('Accent Messenger could not schedule old add-on removal')
            if failed:
                gui.messageBox('NVDA could not remove: %s. Remove these packages in NVDA\'s '
                               'add-on manager, then restart NVDA.' % ', '.join(failed),
                               title, wx.OK | wx.ICON_WARNING)
            elif gui.messageBox('The experimental package will be removed when NVDA restarts. '
                                'Restart now to finish the migration?', title,
                                wx.YES_NO | wx.ICON_INFORMATION) == wx.YES:
                import core
                core.restart()
        except Exception:
            log.exception('Accent Messenger migration prompt failed')

    def terminate(self):
        self._closed = True
        if self._timer is not None:
            self._timer.Stop()
        super().terminate()
