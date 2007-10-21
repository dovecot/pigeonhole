require [
      "fileinto",
      "reject",
      "vacation",
      "envelope",
      "regex" ];
if header :contains "subject" [
      "un eject",
      "lastname.com/spamoff.htm agreed to" ] {
  keep;
}
elsif header :contains "subject" [
      "ADV:",
      "bounceme",
      "2002 Gov Grants",
      "ADV:ADLT",
      "ADV-ADULT",
      "ADULT ADVERTISEMENT" ] {
  reject text:
  Hello.  The server content filter/spam detector I use has bounced your message. It appears to be spam. 

  I do not accept spam/UCE (Unsolicited Commercial Email). 

Please ask me how to bypass this filter if your email is not UCE.  In that case, I am sorry about this 
highly unusual error.  The filter is >99% accurate.

  (This is an automated message; I will not be aware that your message did not get through if I do not hear from you again.)

  -Firstname

  (P.S. You may also override the filter if you accept the terms at http://www.lastname.com/spamoff.htm, 
         by including "lastname.com/spamoff.htm agreed to." in the subject.)

.
;
}
elsif size :over 10485760 {
  reject text:
   Message NOT delivered!
   This system normally accepts email that is less than 10MB in size, because that is how I configured it.
  You may want to put your file on a server and send me the URL.
  Or, you may request override permission and/or unreject instructions via another (smaller) email.
Sorry for the inconvenience.
   Thanks,
.... Firstname
   (This is an automated message; I will not be aware that your message did not get through if I do not hear from you again.)

   Unsolicited advertising sent to this E-Mail address is expressly prohibited 
   under USC Title 47, Section 227.  Violators are subject to charge of up to 
   $1,500 per incident or treble actual costs, whichever is greater.

.
;
}
elsif header :contains "From" "Firstname@lastname.com" {
  keep;
}
elsif header :contains [
      "Sender",
      "X-Sender",
      "Mailing-List",
      "Delivered-To",
      "List-Post",
      "Subject",
      "To",
      "Cc",
      "From",
      "Reply-to",
      "Received" ] "burningman" {
  fileinto "INBOX.DaBurn";
}
elsif header :contains [
      "Subject",
      "From",
      "Received" ] [
      "E*TRADE",
      "Datek",
      "TD Waterhouse",
      "NetBank" ] {
  fileinto "INBOX.finances.status";
}
elsif header :contains "subject" "[pacbell" {
  fileinto "INBOX.pacbell.dslreports";
}
elsif header :contains "From" [
      "owner-te-wg ",
      "te-wg ",
      "iana.org" ] {
  fileinto "INBOX.lst.IETF";
}
elsif header :contains [
      "Mailing-List",
      "Subject",
      "From",
      "Received" ] [
      "Red Hat",
      "Double Funk Crunch",
      "@economist.com",
      "Open Magazine",
      "@nytimes.com",
      "mottimorell",
      "Harrow Technology Report" ] {
  fileinto "INBOX.lst.interesting";
}
elsif header :contains [
      "Mailing-List",
      "Subject",
      "From",
      "Received",
      "X-LinkName" ] [
      "DJDragonfly",
      "Ebates",
      "Webmonkey",
      "DHJ8091@aol.com",
      "Expedia Fare Tracker",
      "SoulShine",
      "Martel and Nabiel",
      "[ecc]" ] {
  fileinto "INBOX.lst.lame";
}
elsif header :contains [
      "Subject",
      "From",
      "To" ] [
      "guru.com",
      "monster.com",
      "hotjobs",
      "dice.com",
      "linkify.com" ] {
  fileinto "INBOX.lst.jobs";
}
elsif header :contains "subject" "[yaba" {
  fileinto "INBOX.rec.yaba";
}
elsif header :contains [
      "to",
      "cc" ] "scalable@" {
  fileinto "INBOX.lst.scalable";
}
elsif header :contains [
      "Sender",
      "To",
      "Return-Path",
      "Received" ] "NTBUGTRAQ@listserv.ntbugtraq.com" {
  fileinto "INBOX.lst.bugtraq";
}
elsif header :contains "subject" "Wired" {
  fileinto "INBOX.lst.wired";
}
elsif anyof (
     header :contains "From" [
        "postmaster",
        "daemon",
        "abuse" ], 
     header :contains "Subject" [
        "warning:",
        "returned mail",
        "failure notice",
        "undelivered mail" ] ) {
  keep;
}
elsif anyof header :contains "From" "and here I put a whitelist of pretty much all the email addresses in my address book - it's several pages..." {
  fileinto "INBOX.white";
}
elsif anyof (
     envelope :all :is [
        "To",
        "CC",
        "BCC" ] "Firstname.lastname@fastmail.fm", 
     header :matches "X-Spam-score" [
        "9.?",
        "10.?",
        "9",
        "10",
        "11.?",
        "12.?",
        "13.?",
        "14.?",
        "11",
        "12",
        "13",
        "14",
        "15.?",
        "16.?",
        "17.?",
        "18.?",
        "19.?",
        "15",
        "16",
        "17",
        "18",
        "19",
        "2?.?",
        "2?",
        "3?.?",
        "3?",
        "40" ] ) {
  reject text:
  Hello.  The server content filter/spam detector I use has bounced your message. It appears to be spam. 

  I do not accept spam/UCE (Unsolicited Commercial Email). 

Please ask me how to bypass this filter if your email is not UCE.  In that case, I am sorry about this 
highly unusual error.  The filter is >99% accurate.

  (This is an automated message; I will not be aware that your message did not get through if I do not hear from you again.)

  -Firstname

  (P.S. You may also override the filter if you accept the terms at http://www.lastname.com/spamoff.htm, 
         by including "lastname.com/spamoff.htm agreed to." in the subject.)

.
;
}
elsif header :matches "X-Spam" [
      "spam",
      "high" ] {
  if header :matches "X-Spam-score" [
        "5.?",
        "6.?",
        "5",
        "6" ] {
    fileinto "INBOX.Spam.5-7";
  }
  elsif header :matches "X-Spam-score" [
        "7.?",
        "8.?",
        "7",
        "8" ] {
    fileinto "INBOX.Spam.7-9";
  }
}
elsif header :contains [
      "Content-Type",
      "Subject" ] [
      "ks_c_5601-1987",
      "euc_kr",
      "euc-kr" ] {
  fileinto "Inbox.Spam.kr";
}
elsif header :contains "Received" "yale.edu" {
  fileinto "INBOX.Yale";
}
elsif anyof (
     header :contains "Subject" [
        "HR 1910",
        "viagra",
        "MLM",
        "               ",
        "	" ], 
     not exists [
        "From",
        "Date" ], 
     header :contains [
        "Sender",
        "X-Sender",
        "Mailing-List",
        "X-Apparently-From",
        "X-Version",
        "X-Sender-IP",
        "Received",
        "Return-Path",
        "Delivered-To",
        "List-Post",
        "Date",
        "Subject",
        "To",
        "Cc",
        "From",
        "Reply-to",
        "X-AntiAbuse",
        "Content-Type",
        "Received",
        "X-LinkName" ] [
        "btamail.net.cn",
        "@arabia.com" ] ) {
  fileinto "INBOX.GreyMail";
}
elsif header :contains [
      "Precedence",
      "Priority",
      "X-Priority",
      "Mailing-List",
      "Subject",
      "From",
      "Received",
      "X-LinkName" ] [
      "Bulk",
      "Newsletter" ] {
  fileinto "INBOX.Bulk Precedence";
}
elsif header :contains [
      "to",
      "cc",
      "Received" ] [
      "IT@lastname.com",
      "mail.freeservers.com" ] {
  fileinto "INBOX.lastname.IT";
}
elsif header :contains [
      "To",
      "CC" ] "Firstname@lastname.com" {
  fileinto "INBOX.lastname.non-BCC";
}
