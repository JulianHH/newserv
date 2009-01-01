/* Automatically generated by refactor.pl.
 *
 *
 * CMDNAME: ban
 * CMDLEVEL: QCMD_AUTHED | QCMD_ALIAS
 * CMDARGS: 3
 * CMDDESC: Permanently bans a hostmask on a channel.
 * CMDFUNC: csc_dopermban
 * CMDPROTO: int csc_dopermban(void *source, int cargc, char **cargv);
 * CMDHELP: Usage: BAN <channel> <hostmask> [<reason>]
 * CMDHELP: Permanently bans the provided hostmask on the channel.  If the ban is
 * CMDHELP: removed from the channel e.g. by a channel op or the BANTIMER feature, the
 * CMDHELP: ban will be reapplied if a matching user joins the channel.  Bans
 * CMDHELP: set with the PERMBAN command can be removed with BANCLEAR or BANDEL.  Any users
 * CMDHELP: matching the hostmask will be kicked from the channel.
 * CMDHELP: Where:
 * CMDHELP: channel  - channel to set a ban on
 * CMDHELP: hostmask - hostmask (nick!user@host) to ban.
 * CMDHELP: reason   - reason for the ban.  This will be used in kick messages when kicking
 * CMDHELP:            users matching the ban.  If this is not provided the generic message
 * CMDHELP:            \"Banned.\" will be used.
 * CMDHELP: BAN requires master (+m) access on the named channel.
 * CMDHELP: BAN is an alias for PERMBAN.
 */

/* code in permban.c */