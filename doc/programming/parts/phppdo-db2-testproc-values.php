$stmt=$dbh->prepare("call exampleproc(?,?,?,?,?,?)");
$stmt->bindValue("1",1);
$stmt->bindValue("2","1.1");
$stmt->bindValue("3","hello");
$out4=0;
$stmt->bindParam("4",$out4,PDO::PARAM_INT|PDO::PARAM_INPUT_OUTPUT);
$out5="";
$stmt->bindParam("5",$out5,PDO::PARAM_STR|PDO::PARAM_INPUT_OUTPUT);
$out6="";
$stmt->bindParam("6",$out6,PDO::PARAM_STR|PDO::PARAM_INPUT_OUTPUT);
$result=$stmt->execute();
