<?php
include_once 'utils.inc';

function processAlertsForJob($jobId) {
  global $wptValidationStateCodes;
  global $wptResultStatusCodes;

  $baseurl = getBaseURL();

  $jobTable = Doctrine_Core::getTable('WPTJob');
  $job = $jobTable->find($jobId);
  // $link = $baseurl."listResults.php?currentPage=1&orderBy=Date&orderByDir=DESC&filterField=WPTJob.Id&filterValue=" . $jobId;
  // direct link to the WPT test result

  // Fetch all related alert ids from the link table
  $q = Doctrine_Query::create()->from('WPTJob_Alert a')->where('a.WPTJobId = ?', $jobId);
  $alerts = $q->execute();
  $q->free(true);

  $alertsTable = Doctrine_Core::getTable('Alert');
	$nbAlerts = 0;
  foreach ($alerts as $alertId) {
    $alert = $alertsTable->find($alertId['AlertId']);

    // Skip inactive alerts
    if (!$alert['Active']) {
      continue;
    }
    // logOutput("[INFO] [processAlertsForJob] Job: ".$job['Label']);

    // Set last alert to 0 if not set already
    if (!$lastAlert = $alert['LastAlertTime']) {
      $lastAlert = 0;
    }

    $alertOn = $alert['AlertOn'];
    $comparator = $alert['AlertOnComparator'];
    $threshold = $alert['AlertThreshold'];
    $alertEmailAddresses = $alert['EmailAddresses'];
    $alertType = $alert['AlertOnType'];
    $alertOnValue = $alert['AlertOnValue'];

    if ($comparator == "equals") {
      $comparator = "=";
    } else if ($comparator == "not equals") {
      $comparator = "!=";
    } else if ($comparator == "greater than") {
      $comparator = ">";
    } else if ($comparator == "less than") {
      $comparator = "<";
    }

    // Fetch all the results after the last alert date that aren't still in pending mode
    $q = Doctrine_Query::create()->from('WPTResult r')
        ->where('r.Date > ?', $lastAlert)
        ->andWhere('r.WPTJobId = ?', $jobId)
        ->andWhere('r.Status > ?', 100);

	$alertLabel = makeLabelFromJobID($jobId);

    // Return if non found
	if ($q->count() < 1) {
    // UNCOMMENT if you want to use the WPT alerts in your own system
		/*exportResultToExternal('alerts.'.$alertLabel.'.AvgFirstViewFirstByte', 0);
		exportResultToExternal('alerts.'.$alertLabel.'.AvgFirstViewStartRender', 0 );
    */
		return;
	}
	
	// jobId;
	
    // Response code type
    if ($alertType == "Response Code") {
      $q = Doctrine_Query::create()->from('WPTResult r')
          ->where('r.Status ' . $comparator . ' ?', $alertOn)
          ->andWhere('r.Date > ?', $lastAlert)
          ->andWhere('r.WPTJobId = ?', $jobId)
          ->andWhere('r.Status > ?', 100);

      // Has the threshold been passed?
      if ($q->count() > $threshold) {

        // Update the last alert time
        $alert['LastAlertTime'] = current_seconds();
        $alert->save();
        $q->free(true);

        logOutput("[INFO] [processAlertsForJob] Alerting on Job: " . $job['Label'] . " for Alert: " . $alertId . " - " . $alert['Label']);

        $message = "Alert \n" .
            "JobId: " . $jobId . " - " . $job['Label'] .
            " \nAlertId: " . $alertId . " - " . $alert['Label'] . " \n" .
            "Response Code was " . $wptResultStatusCodes[$alertOn] . " \n" .
            "Threshold: " . $alert['AlertThreshold'];
        $message = $message . " \n" . $link;
        sendEmailAlert($alertEmailAddresses, $message);
		$nbAlerts++;
      }
    } else if ($alertType == "Validation Code") {

      $q = Doctrine_Query::create()->from('WPTResult r')
          ->where('r.ValidationState ' . $comparator . ' ?', $alertOn)
          ->andWhere('r.Date > ?', $lastAlert)
          ->andWhere('r.WPTJobId = ?', $jobId)
          ->andWhere('r.Status > ?', 100);

      if ($q->count() > $threshold) {
        $alert['LastAlertTime'] = current_seconds();
        $alert->save();
        $q->free(true);

        logOutput("[INFO] [processAlertsForJob] Alerting on Job: " . $job['Label'] . " for Alert: " . $alertId . " - " . $alert['Label']);

        $message = "Alert \n" .
            "JobId: " . $jobId . " - " . $job['Label'] .
            " \nAlertId: " . $alertId . " - " . $alert['Label'] . " \n" .
            "Validation code was " . $wptValidationStateCodes[$alertOn];

        $message = $message . " \n" . $link;
        sendEmailAlert($alertEmailAddresses, $message);
		$nbAlerts++;
      }

    } else if ($alertType == "Response Time") {
      $q = Doctrine_Query::create()->from('WPTResult r')
          ->where('r.' . $alertOn . ' ' . $comparator . ' ?', $alertOnValue)
          ->andWhere('r.Date > ?', $lastAlert)
          ->andWhere('r.WPTJobId = ?', $jobId)
          ->andWhere('r.Status > ?', 100);

      if ($q->count() > $threshold) {
        $alert['LastAlertTime'] = current_seconds();
        $alert->save();
        $q->free(true);
        logOutput("[INFO] [processAlertsForJob] Alerting on Job: " . $job['Label'] . " for Alert: " . $alertId . " - " . $alert['Label']);

        $message = "Alert \n" .
            "JobId: " . $jobId . " - " . $job['Label'] .
            " \nAlertId: " . $alertId . " - " . $alert['Label'] . " \n" .
            $alert['AlertOn'] . " was " .
            $alert['AlertOnComparator'] . ' ' . $alert['AlertOnValue'] . " \n" .
            "Threshold: " . $alert['AlertThreshold'];
        $message = $message . " \n" . $link;

		exportResultToExternal('alerts.'.$alertLabel.'.'.$alert['AlertOn'], 1);

        sendEmailAlert($alertEmailAddresses, $message);
		$nbAlerts++;
      }
    }
  }
	return $nbAlerts;
}

function sendEmailAlert($emailAddresses, $message) {
  $configTable = Doctrine_Core::getTable('WPTMonitorConfig');
  $config = $configTable->find(1);

  $alertFrom = $config['SiteAlertFromName'];
  $alertFromEmail = $config['SiteAlertFromEmailAddress'];
  $alertMessage = $config['SiteAlertMessage'];
  $siteName = $config['SiteName'];
  if (strrpos($emailAddresses, ",")>0) {
    $addrs = explode(",", $emailAddresses);
  } else {
    $addrs = explode("\n", $emailAddresses);
  }
  $success = 1;
  foreach ($addrs as $emailAddress) {
    $to = $emailAddress;
    $subject = $siteName . " - WPTMonitor Alert";
    $headers = 'From: ' . $alertFrom . ' <' . $alertFromEmail . ">\r\n" .
        'Reply-To: ' . $alertFromEmail . "\r\n" .
        'Content-type: text/plain; charset=UTF-8' . "\r\n" .
        'X-Mailer: PHP/' . phpversion();
    if (mail($to, $subject, $alertMessage . "\n\n" . $message, $headers)) {
      logOutput("[INFO] [sendEmailAlert] Email alert sent to contact: ".$emailAddress);
      $success = 1;
    } else {
      logOutput("[INFO] [sendEmailAlert] Failed to send alert email to contact");
      $success = 0;
    }
  }
  return $success;
}

