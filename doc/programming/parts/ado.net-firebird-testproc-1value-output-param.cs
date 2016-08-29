SQLRelayCommand sqlrcom = (SQLRelayComand)sqlrcon.CreateCommand();
sqlrcom.CommandText = "execute procedure testproc ?, ?, ?";
sqlrcom.Parameters.Add("1",1);
sqlrcom.Parameters.Add("2",1.1);
sqlrcom.Parameters.Add("3","hello");
SQLRelayParameter out1 = new SQLRelayParameter();
out1.ParameterName = "1";
out1.Direction = ParameterDirection.Output;
out1.DbType = DbType.Int64;
sqlrcom.Parameters.Add(out1);
sqlrcom.ExecuteNonQuery();
Int64 result = Convert.ToInt64(out1.Value);
