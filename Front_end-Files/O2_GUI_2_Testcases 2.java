package ui2;
import java.io.File;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Set;
import java.util.TreeSet;

import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JFrame;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.table.DefaultTableModel;
import javax.swing.table.TableModel;
import javax.swing.GroupLayout.Alignment;
import javax.swing.GroupLayout;
import javax.swing.LayoutStyle.ComponentPlacement;
import java.awt.event.ActionListener;
import java.awt.Dimension;
import java.awt.event.ActionEvent;
/**
 * @author Lavanya
 */
@SuppressWarnings("serial")
public class O2_GUI_2_Testcases 2 extends JFrame {
	public O2_GUI_2_Testcases 2() {
	}

	public void O2_GUI_2_Testcases 2(ArrayList arr) throws IOException {
    	initComponents(arr);
    }

  
    void initComponents(ArrayList<String> arr) throws IOException { 
    	Set<String> set = new TreeSet();
    	for(int i = 0; i < arr.size(); i++) {
    	    set.add(arr.get(i));
    	}
    	
    	String[] array = new String[set.size()];
    	
    	int i=0;
    	for(String s:set)
    		array[i++]=s;
		    	
    	JComboBox SelectTc = new JComboBox(array);
    	File folder = new File("C:\\Users\\Lavu\\Desktop\\CMU\\CMU\\O2_Project\\O2_Latest\\o2-master\\o2-master\\test");
    	File[] listOfFiles = folder.listFiles();
    	ArrayList<String> list = new ArrayList<String>();
    	
    	for (File file : listOfFiles) {
    		String filename = file.getName();
    		String ext = "c";
    		String extension = filename.substring(filename.lastIndexOf(".") + 1, filename.length());
    		if (ext.equals(extension)) {
    				list.add(filename);
    	    }
    	}
   	
        jScrollPane1 = new JScrollPane();
        jTable1 = new JTable();
        jButton_add_rows = new JButton();
        jScrollPane2 = new JScrollPane();
        jTable2 = new JTable();
        
        DefaultTableModel model = new DefaultTableModel(new Object[]{"Testcases"}, 0);
        for(String item : list){
             model.addRow(new Object[]{item});
        }
    	this.jTable1.setModel(model);
        setDefaultCloseOperation(javax.swing.WindowConstants.EXIT_ON_CLOSE);
        jScrollPane1.setViewportView(jTable1);
        jButton_add_rows.setText("Remove Selected");
        jButton_add_rows.addActionListener(new java.awt.event.ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent evt) {
                jButton_remove_rowsActionPerformed(evt);
            }
        });
        jTable2.setModel(new javax.swing.table.DefaultTableModel(
                new Object [][] {},
                new String [] {
                    "Selected Testcases"
                }
            ));
        jScrollPane2.setViewportView(jTable2);
        
        JButton btnRemoveAll = new JButton();
        btnRemoveAll.setText("Remove All");
        btnRemoveAll.addActionListener(new ActionListener() {
        	public void actionPerformed(ActionEvent evt) {
        		jButton_removeall_rowsActionPerformed(evt);
        	}
        });
        
        JButton btnAddAllRows = new JButton();
        btnAddAllRows.setText("Add All");
        btnAddAllRows.addActionListener(new ActionListener() {
        	public void actionPerformed(ActionEvent evt) {
        		jButton_addall_rowsActionPerformed(evt);
        	}
        });
        
        JButton btnRemoveSelected = new JButton();
        btnRemoveSelected.setText("Add Selected");
        btnRemoveSelected.addActionListener(new ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent evt) {
                jButton_add_rowsActionPerformed(evt);
            }
        });
        
        JButton Execute = new JButton();
        Execute.setText("Execute");
        
        JButton Back = new JButton();
        Back.setText("Back");
        Back.addActionListener(new ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent evt) {
        		O2_GUI_2_Testcases GU = new O2_GUI_2_Testcases();
            	GU.setVisible(false);
            	dispose();
            }    });
        

        javax.swing.GroupLayout layout = new javax.swing.GroupLayout(getContentPane());
        
        layout.setHorizontalGroup(
        	layout.createParallelGroup(Alignment.LEADING)
        		.addGroup(layout.createSequentialGroup()
        			.addContainerGap()
        			.addComponent(jScrollPane1, GroupLayout.PREFERRED_SIZE, 265, GroupLayout.PREFERRED_SIZE)
        			.addGap(20)
        			.addGroup(layout.createParallelGroup(Alignment.LEADING)	
        				.addComponent(SelectTc, GroupLayout.DEFAULT_SIZE, 250, GroupLayout.PREFERRED_SIZE)
        				.addGap(20)
        				.addComponent(jButton_add_rows, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
        				.addComponent(btnRemoveSelected, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
        				.addComponent(btnAddAllRows, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
        				.addComponent(btnRemoveAll, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
            			.addComponent(Execute, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
        			.addComponent(Back, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE))  
        			.addPreferredGap(ComponentPlacement.RELATED)
        			.addComponent(jScrollPane2, GroupLayout.PREFERRED_SIZE, 265, GroupLayout.PREFERRED_SIZE)        			
        			.addGap(31))
        );
        layout.setVerticalGroup(
        	layout.createParallelGroup(Alignment.LEADING)
        		.addGroup(layout.createSequentialGroup()
        			.addGroup(layout.createParallelGroup(Alignment.LEADING)
        				.addGroup(layout.createSequentialGroup()
        					.addGap(95)
        					.addComponent(SelectTc)
        					.addGap(80)
        					.addComponent(btnRemoveSelected)
        					.addPreferredGap(ComponentPlacement.RELATED)
        					.addComponent(btnAddAllRows, GroupLayout.DEFAULT_SIZE, 23, Short.MAX_VALUE)
        					.addPreferredGap(ComponentPlacement.RELATED)
        					.addComponent(jButton_add_rows)
        					.addPreferredGap(ComponentPlacement.RELATED)
        					.addComponent(btnRemoveAll, GroupLayout.DEFAULT_SIZE, GroupLayout.DEFAULT_SIZE, Short.MAX_VALUE)
        					.addGap(20)
        					.addComponent(Execute, GroupLayout.DEFAULT_SIZE, GroupLayout.DEFAULT_SIZE, Short.MAX_VALUE)
        					.addGap(20)
        					.addComponent(Back, GroupLayout.DEFAULT_SIZE, GroupLayout.DEFAULT_SIZE, Short.MAX_VALUE)
        					.addGap(100))
        				.addGroup(layout.createSequentialGroup()
        					.addContainerGap()
        					.addGroup(layout.createParallelGroup(Alignment.BASELINE)
        						.addComponent(jScrollPane2, GroupLayout.PREFERRED_SIZE, GroupLayout.DEFAULT_SIZE, GroupLayout.PREFERRED_SIZE)
        						.addComponent(jScrollPane1, GroupLayout.PREFERRED_SIZE, GroupLayout.DEFAULT_SIZE, GroupLayout.PREFERRED_SIZE))))
        			.addGap(39))
        );
        getContentPane().setLayout(layout);

        pack();
    }// </editor-fold>                        

	private void jButton_add_rowsActionPerformed(java.awt.event.ActionEvent evt) {                                                 

        TableModel model = jTable1.getModel();
        int[] indexs = jTable1.getSelectedRows();
        Object[] row = new Object[1];
        DefaultTableModel model2 = (DefaultTableModel) jTable2.getModel();
        for(int i = 0; i < indexs.length; i++)
        {
            row[0] = model.getValueAt(indexs[i], 0);
            model2.addRow(row);
        } 
    }    
    
    private void jButton_addall_rowsActionPerformed(java.awt.event.ActionEvent evt) {                                                  
        int rowCount = jTable1.getRowCount();
        Object[] row = new Object[rowCount];
        System.out.println(jTable1.getValueAt(0,0));
        for(int k=0;k<rowCount;k++){
        row[k]=jTable1.getValueAt(k,0);
        }
        for(int k=0;k<rowCount;k++){
            System.out.println(row[k]);
            }
        
        DefaultTableModel model2 = (DefaultTableModel)jTable2.getModel();
        for(int i = 0; i < row.length; i++)
        {
            row[0] = row[i];
            model2.addRow(row);
        } 
    }  
    
    
    private void jButton_remove_rowsActionPerformed(java.awt.event.ActionEvent evt) {
        int[] indexs = jTable2.getSelectedRows();
        DefaultTableModel model2 = (DefaultTableModel) jTable2.getModel();        
        for(int i = 0; i < indexs.length; i++)
        {
        	model2.removeRow(jTable2.getSelectedRow());
        }
    } 
    
   
    private void jButton_removeall_rowsActionPerformed(java.awt.event.ActionEvent evt) {                                                       
        DefaultTableModel model2 = (DefaultTableModel)jTable2.getModel();
        int rowCount = model2.getRowCount();
        for (int i = rowCount - 1; i >= 0; i--) {
        	model2.removeRow(i);
        }
    } 
    
 
    
    /**
     * @param args the command line arguments
     */
    public static void main(String args[]) {
        try {
            for (javax.swing.UIManager.LookAndFeelInfo info : javax.swing.UIManager.getInstalledLookAndFeels()) {
                if ("Nimbus".equals(info.getName())) {
                    javax.swing.UIManager.setLookAndFeel(info.getClassName());
                    break;
                }
            }
        } catch (ClassNotFoundException ex) {
            java.util.logging.Logger.getLogger(O2_GUI_2_Testcases.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        } catch (InstantiationException ex) {
            java.util.logging.Logger.getLogger(O2_GUI_2_Testcases.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        } catch (IllegalAccessException ex) {
            java.util.logging.Logger.getLogger(O2_GUI_2_Testcases.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        } catch (javax.swing.UnsupportedLookAndFeelException ex) {
            java.util.logging.Logger.getLogger(O2_GUI_2_Testcases.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        }
        //</editor-fold>

        /* Create and display the form */
        java.awt.EventQueue.invokeLater(new Runnable() {
            public void run() {
                new O2_GUI_2_Testcases().setVisible(true);
            }
        });
    }

    // Variables declaration - do not modify                     
    private javax.swing.JButton jButton_add_rows;
    private javax.swing.JScrollPane jScrollPane1;
    private javax.swing.JScrollPane jScrollPane2;
    private javax.swing.JTable jTable1;
    private javax.swing.JTable jTable2;

}