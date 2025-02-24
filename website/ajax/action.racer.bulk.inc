<?php
// $_POST['who']:
//    'all' => all racers
//    'd' + partitionid => all members of a partition
//    'c' + classid => all members of a class
//    'r' + rankid => all members of one rank
//
// $_POST['what']:
//    'checkin' => set 'passed' value
//    'number' => assign car numbers
//    'eligibility' => change racer eligibility
//
// $_POST['value'] (for checkin or eligible): 1 or 0
//
// $_POST['start'] (for number only): number to assign to the first car
//
// $_POST['renumber'] (for number only): if present, renumber all members of the
// group, otherwise only those without a non-zero car number.
//

require_once('inc/events.inc');

$ok = true;
if (!have_permission(EDIT_RACER_PERMISSION)) {
  not_authorized_body();
  $ok = false;
}

if ($ok) {
  $sql = 'SELECT racerid FROM RegistrationInfo ';
  $params = array();
  if ($_POST['who'] == 'all') {
    $where = '';
  } else if (substr($_POST['who'], 0, 1) == 'd') {
    $where = 'WHERE partitionid = :partitionid';
    $params[':partitionid'] = substr($_POST['who'], 1);
  } else if (substr($_POST['who'], 0, 1) == 'c') {
    $where = 'WHERE classid = :classid';
    $params[':classid'] = substr($_POST['who'], 1);
  } else if (substr($_POST['who'], 0, 1) == 'r') {
    $where = 'WHERE rankid = :rankid';
    $params[':rankid'] = substr($_POST['who'], 1);
  } else {
    json_failure('who', "Unrecognized 'who' parameter: ".$_POST['who']);
    $ok = false;
  }
}


if ($ok) {
  if ($_POST['what'] == 'number') {
    $car_number = intval($_POST['start']);
    $update_fn = 'update_number';
    $update_sql = $db->prepare('UPDATE RegistrationInfo'
                               .' SET carnumber = :carnumber'
                               .' WHERE racerid = :racerid');
  } else if ($_POST['what'] == 'checkin') {
    $value = isset($_POST['value']) ? $_POST['value'] : 1;
    $update_fn = 'update_checkin';
    $update_sql = $db->prepare('UPDATE RegistrationInfo'
                               .' SET passedinspection = :value'
                               .' WHERE racerid = :racerid');
  } else if ($_POST['what'] == 'eligibility') {
    $value = isset($_POST['value']) ? $_POST['value'] : 1;
    $update_fn = 'update_eligible';
    $update_sql = $db->prepare('UPDATE RegistrationInfo'
                               .' SET exclude = :value'
                               .' WHERE racerid = :racerid');
  } else {
    json_failure('what', "Unrecognized 'what' parameter: ".$_POST['what']);
    $ok = false;
  }
}

if ($ok && $update_fn == 'update_number') {
  if (isset($_POST['renumber']) && $_POST['renumber']) {
    // If renumbering, then no restriction
  } else {
    if (strlen($where) == 0) {
      $where = 'WHERE ';
    } else {
      $where .= ' AND ';
    }
    // Even though carnumber is an INTEGER, it can apparently sometimes take the value of
    // empty string, which is different from zero or null
    $where .= '(carnumber IS NULL OR carnumber = \'\' OR carnumber = 0)';
  }
}

$racer_renumberings = array();  // racerid to (new) car number

function update_number($racerid) {
  global $update_sql;
  global $car_number;
  global $racer_renumberings;

  // echo '<numbered racerid="'.$racerid.'" carnumber="'.$car_number.'"/>'."\n";

  $update_sql->execute(array(':carnumber' => $car_number,
                             ':racerid' => $racerid));

  $racer_renumberings[$racerid] = $car_number;

  ++$car_number;
  return true;
}

function update_checkin($racerid) {
  global $update_sql;
  global $value;

  if (0 + read_single_value('SELECT COUNT(*) FROM RaceChart'
                               .' WHERE roundid IN (SELECT DISTINCT roundid FROM RaceChart'
                               .'                   WHERE racerid = :racerid)'
                               .' AND (finishtime IS NOT NULL OR finishplace IS NOT NULL)',
                               array(':racerid' => $racerid), 0)) {
    json_failure('alreadyscheduled', "Schedule data already exists for this racer");
    return false;
  }

  if (!$value && !have_permission(REVERT_CHECK_IN_PERMISSION)) {
    not_authorized_body();
    return false;
  }

  $update_sql->execute(array(':value' => $value,
                             ':racerid' => $racerid));
  record_event(EVENT_CHECKIN, array('racerid' => $racerid));
  return true;
}

function update_eligible($racerid) {
  global $update_sql;
  global $value;

  // $value is 1 for eligible, stored as exclude=0
  $update_sql->execute(array(':value' => 1 - $value,
                             ':racerid' => $racerid));
  return true;
}

if ($ok) {
  $stmt = $db->prepare($sql . $where);
  $stmt->execute($params);
  foreach ($stmt as $racer) {
    if ($ok) {
      $ok = call_user_func($update_fn, $racer['racerid']);
    }
  }
}

if ($ok) {
  if (!empty($racer_renumberings)) {
    record_action(array('action' => 'racer.*renumber', 'results' => $racer_renumberings));
  }

  json_success();
}

json_out('reload', true);
