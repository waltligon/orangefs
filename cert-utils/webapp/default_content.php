<div class="section">
	<div class="section-content">
		<h2>OrangeFS Certificates Download</h2>
		<p>
			Certificates provide access control for your files.
		</p>
	</div>
</div>
<div class="section bordered">
	<div class="section-content">
		<h4>User Certificate</h4>
		<p id="user_cert">
			This certificate identifies you to OrangeFS.
			<br />
			<form id="user_cert_form" method="get">
				<input type="hidden" name="t" value="user"/>
			</form>
		</p>
		<a href="#user_cert" onclick="dlUserCert()">download</a>
	</div>
</div>
<div class="section bordered">
	<div class="section-content">
		<h4>Proxy Certificate</h4>
		<p id="proxy_cert">
			This certificate authorizes you for OrangeFS services.
			<br />
			<form id="proxy_cert_form" method="get">
				<input type="hidden" name="t" value="proxy"/>
				Expires in
				<input type="number" name="exp" id="proxy_cert_exp" value="14" />
				days
				<br />
			</form>
		</p>
		<a href="#proxy_cert" onclick="dlProxyCert()">download</a>
	</div>
</div>