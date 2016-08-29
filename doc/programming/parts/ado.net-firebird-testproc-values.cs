SQLRelayCommand sqlrcom = (SQLRelayComand)sqlrcon.CreateCommand();
sqlrcom.CommandText = "select * from testproc(?,?,?)";
sqlrcom.Parameters.Add("1",1);
sqlrcom.Parameters.Add("2",1.1);
sqlrcom.Parameters.Add("3","hello");
sqlrcom.ExecuteReader();
System.Data.IDataReader datareader = sqlrcom.ExecuteReader();
datareader.Read();
Int64 out1 = datareader.GetInt64(0);
Double out2 = datareader.GetDouble(1);
String out3 = datareader.GetString(2);
