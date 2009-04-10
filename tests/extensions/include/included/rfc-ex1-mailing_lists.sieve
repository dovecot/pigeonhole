require ["fileinto"];

if header :is "Sender" "owner-ietf-mta-filters@imc.org"
{
     fileinto "lists.sieve";
}
elsif header :is "Sender" "owner-ietf-imapext@imc.org"
{
     fileinto "lists.imapext";
}
