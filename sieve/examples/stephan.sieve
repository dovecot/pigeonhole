# Sieve filter

require "fileinto";

if header :contains "X-RenameIT-MailScanner-SpamScore" "sssss" {
  redirect "spam@drunksnipers.com";
  stop;
}

if header :contains "X-Mailing-List" "linux-kernel@vger.kernel.org" {
  fileinto "inbox.INBOX.Kernel.linux-kernel";
  stop;
}

if header :contains "List-Id" "mspgcc-users.lists.sourceforge.net" {
  fileinto "inbox.INBOX.Embedded Systems.mspgcc";
  stop;
} 

if header :contains "List-Id" "ietf-imapext.imc.org" {
  fileinto "inbox.INBOX.Internet Standards Mailinglists.ietf-imapext";
  stop;
}

if header :contains "List-Id" "ietf-mta-filters.imc.org" {
  fileinto "inbox.INBOX.Internet Standards Mailinglists.ietf-mta-filters";
  stop;
}

if header :contains "List-Id" "imap-protocol.u.washington.edu" {
  fileinto "inbox.INBOX.Internet Standards Mailinglists.imap-protocol";
  stop;
}

if header :contains "List-Id" "lemonade.ietf.org" {
  fileinto "inbox.INBOX.Internet Standards Mailinglists.lemonade";
  stop;
}

if header :contains "List-Id" "exim-users.exim.org" {
  fileinto "inbox.INBOX.Software Mailinglists.exim-users";
  stop;
}

if header :contains "List-Id" "exim-dev.exim.org" {
  fileinto "inbox.INBOX.Software Mailinglists.exim-dev";
  stop;
}

if header :contains "List-Id" "courier-imap.lists.sourceforge.net" {
  fileinto "inbox.INBOX.Software Mailinglists.courier-imap";
  stop;
}

if header :contains "List-Id" "dovecot.dovecot.org" {
  fileinto "inbox.INBOX.Software Mailinglists.dovecot";
  stop;
}

if header :contains "List-Id" "dovecot-cvs.dovecot.org" {
  fileinto "inbox.INBOX.Software Mailinglists.dovecot-cvs";
  stop;
}

if header :contains "List-Id" "mailman-developers.python.org" {
  fileinto "inbox.INBOX.Software Mailinglists.mailman-dev";
  stop;
}

if header :contains "List-Id" "mailman3-dev.python.org" {
  fileinto "inbox.INBOX.Software Mailinglists.mailman3-dev";
  stop;
}

if header :contains "List-Id" "mailscanner.lists.mailscanner.info" {
  fileinto "inbox.INBOX.Software Mailinglists.mailscanner";
  stop;
}

if header :contains "List-Id" "openvpn-devel.lists.sourceforge.net" {
  fileinto "inbox.INBOX.Software Mailinglists.openvpn-devel";
  stop;
}

if header :contains "List-Id" "sare-users.maddoc.net" {
  fileinto "inbox.INBOX.Software Mailinglists.sare-users";
  stop;
}

if header :contains "List-Id" "twisted-python.twistedmatrix.com" {
  fileinto "inbox.INBOX.Software Mailinglists.twisted-python";
  stop;
}

if header :contains "List-Id" "vtun-devel.lists.sourceforge.net" {
  fileinto "inbox.INBOX.Software Mailinglists.vtun-devel";
  stop;
}

if header :contains "List-Id" "users.spamassassin.apache.org" {
  fileinto "inbox.INBOX.Software Mailinglists.spamassassin-users";
  stop;
}

if header :contains "List-Id" "dev.spamassassin.apache.org" {
  fileinto "inbox.INBOX.Software Mailinglists.spamassassin-dev";
  stop;
}

if header :contains "List-Id" "kde-pim.kde.org" {
  fileinto "inbox.INBOX.Software Mailinglists.kde-pim";
  stop;
}

if header :contains "List-Id" "kde-perl.kde.org" {
  fileinto "inbox.INBOX.Software Mailinglists.kde-perl";
  stop;
}

if header :contains "List-Post" "ut2004@icculus.org" {
  fileinto "inbox.INBOX.Unreal Tournament.ut2004@icculus";
  stop;
}

if header :contains "List-Id" "ut2004servers.udn.epicgames.com" {
  fileinto "inbox.INBOX.Unreal Tournament.ut2004-servers";
  stop;
}

if anyof (
  header :contains "To" "beheer@vestingbar.",
  header :contains "Cc" "beheer@vestingbar.",
  header :contains "To" "-admin@vestingbar.",
  header :contains "To" "-owner@vestingbar." ) {
  fileinto "inbox.INBOX.Vestingbar.beheer@vestingbar";
  stop;
}

if anyof (
  header :contains "To" "nico@vestingbar.",
  header :contains "Cc" "nico@vestingbar.",
  header :contains "To" "flowview@vestingbar.",
  header :contains "Cc" "flowview@vestingbar.",
  header :contains "List-Id" "td.vestingbar.nl",
  header :contains "List-Id" "all.vestingbar.nl"  ) {
  fileinto "inbox.INBOX.Vestingbar.nico@vestingbar";
  stop;
}

if header :contains "To" "abuse@" {
  fileinto "inbox.INBOX.System Admin.abuse";
  stop;
}

if header :contains "To" "spammaster@" {
  fileinto "inbox.INBOX.System Admin.spammaster";
  stop;
}

if header :contains "To" "virusmaster@" {
  fileinto "inbox.INBOX.System Admin.virusmaster";
  stop;
}

if header :contains "To" "postmaster@" {
  fileinto "inbox.INBOX.System Admin.postmaster";
  stop;
}

if header :contains "To" "postmaster@" {
  fileinto "inbox.INBOX.System Admin.webmaster";
  stop;
}

if anyof( 
  header :contains "To" "@cola.", 
  header :contains "Cc" "@cola." ) {

  fileinto "inbox.INBOX.Servers.cola";
  stop;
}

if anyof(
  header :contains "To" "@sinas.",
  header :contains "Cc" "@sinas." ) {

  fileinto "inbox.INBOX.Servers.sinas";
  stop;
}

if anyof(
  header :contains "To" "@sevenup.",
  header :contains "Cc" "@sevenup." ) {

  fileinto "inbox.INBOX.Servers.sevenup";
  stop;
}

if anyof(
  header :contains "To" "@taksi.",
  header :contains "Cc" "@taksi." ) {

  fileinto "inbox.INBOX.Servers.taksi";
  stop;
}

if anyof(
  header :contains "To" "@rivella.",
  header :contains "Cc" "@rivella." ) {

  fileinto "inbox.INBOX.Servers.rivella";
  stop;
}

if anyof(
  header :contains "To" "@klara.",
  header :contains "Cc" "@klara.",
  header :contains "From" "logcheck@klara." ) {

  fileinto "inbox.INBOX.Servers.klara";
  stop;
}

if anyof(
  header :contains "To" "root@vestingbar.",
  header :contains "To" "mail@vestingbar.",
  header :contains "Cc" "root@vestingber." ) {

  fileinto "inbox.INBOX.Servers.vestingbar";
  stop;
}

# Final rules
if anyof(
  header :contains "To" "stephan@rename-it.nl",
  header :contains "Cc" "stephan@rename-it.nl",
  header :contains "To" "sirius@rename-it.nl",
  header :contains "Cc" "sirius@rename-it.nl" ) {

  fileinto "inbox.INBOX.Accounts.stephan@rename-it";
  stop;
}

if anyof(
  header :contains "To" "stephan@drunksnipers.com",
  header :contains "Cc" "stephan@drunksnipers.com",
  header :contains "To" "sirius@drunksnipers.com",
  header :contains "Cc" "sirius@drunksnipers.com" ) {

  fileinto "inbox.INBOX.Accounts.sirius@drunksnipers";
  stop;
}
