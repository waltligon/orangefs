<?php
	
	$header = file_exists("custom_header.php") ? "custom_header.php" : "default_header.php";
	$content = file_exists("custom_content.php") ? "custom_content.php" : "default_content.php";
	$footer = file_exists("custom_footer.php") ? "custom_footer.php" : "default_footer.php";
	
?>
<html>
	<head>
		<meta name="viewport" content="width=device-width; minimum-scale=1.0; maximum-scale=1.0; user-scalable=no" />
		<title>OrangeFS Certificates</title>
		<link rel="stylesheet" type="text/css" href="default.css"  />
		<?php
			if(file_exists("custom.css")) {
				?>
					<link rel="stylesheet" type="text/css" href="custom.css"  />
				<?php
			}
		?>
	</head>
	<script type="text/javascript">
		var dlCertUrl = "cgi-bin/download.pl";

		function dlUserCert(e) {
			var form = document.getElementById("user_cert_form");
			form.action = dlCertUrl;
			form.submit();

			return false;
		}

		function dlProxyCert(e) {
			var exp = document.getElementById("proxy_cert_exp");
			var days = parseInt(exp.value);

			if(isNaN(days) || days < 1) {
				alert("The expiration is the number of days until the\n" + "certificate will expire.");
				return false;
			} else if(days > 14) {
				alert("The maximum number of days is 14.");
				return false;
			}

			var form = document.getElementById("proxy_cert_form");
			form.action = dlCertUrl;
			form.submit();

			return false;
		}
	</script>
	<body>
		<div id="container">
			<div id="header">
				<?php include($header); ?>
			</div>
			<div id="content">
				<?php include($content); ?>
			</div>
			<div id="separator"></div>
			<div id="footer">
				<?php include($footer); ?>
			</div>
		</div>
	</body>
</html>
