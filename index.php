<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">

<title>::: HighWire Development Project - highwire.atari-users.net :::</title>

<LINK href="./stylesheet.css" type=TEXT/CSS rel=stylesheet>
<style>
  .rss-header1 	{text-decoration:none; color: #DBAE67; FONT: 27px Verdana, Helvetica; FONT-WEIGHT: bold}
  .forum1 A:LINK {text-decoration: none; color: #808080; FONT: 10px Verdana, Helvetica; TEXT-DECORATION: none;}
  .forum1 A:VISITED  {text-decoration: none; color: #000000; FONT: 10px Verdana, Helvetica; TEXT-DECORATION: none;}
  .forum1 A:HOVER  {text-decoration: none; color: #0f0f0f; FONT: 10px Verdana, Helvetica; TEXT-DECORATION: none;}
  .forum1 A:HOVER    {BACKGROUND: underline; COLOR: #0f0f0f; FONT: 10px Verdana, Helvetica; TEXT-DECORATION: underline;}
  .forum2 A:LINK     {text-decoration: none; color: #965C00; FONT: 10px Verdana, Helvetica; TEXT-DECORATION: underline; font-weight: bold;}
  .forum2 A:VISITED  {text-decoration: none; color: #965C00; FONT: 10px Verdana, Helvetica; TEXT-DECORATION: underline; font-weight: bold;}
  .forum2 A:HOVER  {text-decoration: none; color: #000000; FONT: 10px Verdana, Helvetica; TEXT-DECORATION: underline; font-weight: bold;}
  .forum2 A:HOVER    {BACKGROUND: underline; COLOR: #000000; FONT: 10px Verdana, Helvetica; TEXT-DECORATION: underline; font-weight: bold;}
</style>

</head>


<?php

// $first = $_GET['first'];
// $section = $_GET['section'];

if(isset($_REQUEST["first"]))
{
        $first = $_REQUEST["first"];
}
 
$section = '';
if(isset($_REQUEST["section"]))
{
        $section = $_REQUEST["section"];
}


$first = '';
if ($first == "")
		{
$first = 0;
		}
?>


<?php

$limit = $_GET['limit'];

if ($limit == "")
		{
$limit = 3;
		}
?>
<center>
<!--table cellspacing="0" cellpadding="0" width="100%">
<tr><td width="50%" valign="top">

<!-- old table placeholder -->


</td>


<td-->

<!-- INSIDE MAIN TABLE -->

<table cellspacing="0" cellpadding="5" border="0" width="980" style="BORDER: #000000 1px solid;" bgcolor="white">
<tr><td colspan="3" align="center">
<img src="./images/logotst2.gif">

</td></tr>
<tr><td valign="top" width="205">
<!-- span1a -->

<table cellspacing="0" cellpadding="0" border="0" align="left">
<tr><td><img src="./images/1x1transp.gif" height="14" width="1"></td></tr>
<tr><td>&nbsp;</td></tr>
<tr><td><img src="./images/1x1transp.gif" height="12" width="1"></td></tr>
<tr><td>&nbsp;</td></tr>
</table>

</td>

<!-- span1b -->
<!-- MAIN AREA START -->

<?php 
// $section = $_GET['section'];
// $entry = $_GET['entry'];

if(isset($_REQUEST["section"]))
{
	$section = $_REQUEST["section"];
}

if(isset($_REQUEST["entry"]))
{
        $entry = $_REQUEST["entry"];
}

if ($section == "")
	{
$section = "home";
$entry = "welcome";
	}
?>
<td valign="top" align="left"><?php include './include/menu.php'; ?> 

<table cellspacing="0" cellpadding="0" border="0" width="100%">

  <tr valign="top">

    <td width="20" colspan="3"><img src="images/up_left.gif"></td>
    <td><img height="4" width="729" src="images/1x1orange.gif"></td>
  </tr>
  <tr valign="top">
    <td height="100%" bgcolor="#ffc66b" width="1"><img width="1" src="./images/1x1_transp.gif"></td>
    <td height="100%" bgcolor="#ffa000" width="3"><img width="1" src="./images/1x1_transp.gif"></td>
    <td width="16"><img height="1" width="1" src="./images/1x1_transp.gif"></td>
    <td>

      <!--contents starts here -->



<?php
if ($section == "home")
	{

	if ($entry == "oldnews")
		{
		include './include/oldnews.php';
		}
	else	{
		include './include/welcome.php';
		}

	} 
else if ($section == "docs")
	{
ob_start();
include './include/hw_doc.htm';
$output = ob_get_contents();
ob_end_clean();

echo iconv("ISO-8859-1", "UTF-8", $output);
	}
else if ($section == "snap")
	{

	if ($entry == "2")
		{
		include './include/screenshots2.php';
		}
	else 
		{
		include './include/screenshots.php';
		}


	}
else if ($section == "file")
	{
include './include/download.php';
	}
else if ($section == "dev")
	{

	if ($entry == "coders")
		{
		include './include/coders.php';
		}
	else if ($entry == "hwdom")
		{
		echo "<pre>";
		include './include/hwdom.txt';
		echo "</pre>";
		}
	else if ($entry == "develop")
		{
		include './include/develop.php';
		}

	}
?>

      <!--contents ends here -->

    </td></tr></table>

<!-- MAIN AREA END -->

</td>

</tr>
<tr><td colspan="3" align="center">
<?php include './include/footer.htm'; ?> 



</td></tr>

</table>
</center>

<!--/td>
<!-- OUTSIDE MAIN TABLE -->

</td><td width="50%">Right</td></tr></table-->

</body>
</html>